#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

class Event {
public:
    std::string payload;
    int lifetime;

    Event(const std::string& data, int lt) : payload(data), lifetime(lt) {}
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
};

class Monitor {
private:
    std::mutex mtx;
    std::condition_variable cv;
    bool ready;
    bool stopping;
    Event* data;

public:
    Monitor() : ready(false), stopping(false), data(nullptr) {}

    ~Monitor() {
        if (data != nullptr) {
            delete data;
            data = nullptr;
        }
    }

    bool provide(int eventNumber) {
        std::unique_lock<std::mutex> lock(mtx);
        if (ready) {
            return false;
        }
        data = new Event("Событие #" + std::to_string(eventNumber), 100 + eventNumber);
        ready = true;

        std::cout << "[Поставщик] Отправлено: " << data->payload
            << " (адрес объекта: " << static_cast<void*>(data)
            << ", lifetime: " << data->lifetime << ")" << std::endl;
        cv.notify_one();
        return true;
    }

    bool consume() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]() { return ready || stopping; });
        if (stopping && !ready) {
            return false;
        }

        Event* receivedEvent = data;
        data = nullptr;
        ready = false;

        std::cout << "[Потребитель] Получено и обработано: " << receivedEvent->payload
            << " (адрес объекта: " << static_cast<void*>(receivedEvent)
            << ", lifetime: " << receivedEvent->lifetime << ")" << std::endl;
        std::cout << "------------------------------------------------------------" << std::endl;
        delete receivedEvent;
        return true;
    }

    void stop() {
        std::unique_lock<std::mutex> lock(mtx);
        stopping = true;
        cv.notify_all();
    }
};

class Producer {
private:
    Monitor& monitor;
    int totalEvents;

public:
    Producer(Monitor& m, int count) : monitor(m), totalEvents(count) {}

    void run() {
        int currentEvent = 1;
        while (currentEvent <= totalEvents) {
            std::this_thread::sleep_for(std::chrono::seconds(1)); 
            if (monitor.provide(currentEvent)) {
                currentEvent++;
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        monitor.stop();
    }
};

class Consumer {
private:
    Monitor& monitor;

public:
    Consumer(Monitor& m) : monitor(m) {}

    void run() {
        while (monitor.consume()) {
        }
    }
};

int main() {
    setlocale(LC_ALL, "Russian");
    const int EVENTS_COUNT = 5;

    Monitor monitor;
    Producer producer(monitor, EVENTS_COUNT);
    Consumer consumer(monitor);
    std::thread consumerThread(&Consumer::run, &consumer);
    std::thread producerThread(&Producer::run, &producer);
    producerThread.join();
    consumerThread.join();

    std::cout << "Работа монитора успешно завершена." << std::endl;
    return 0;
}