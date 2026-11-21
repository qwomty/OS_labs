#!/bin/sh

EX_USAGE=1
EX_NOFILE=2
EX_TYPE=3
EX_NOOUT=4
EX_NOTOOL=5
EX_MKTEMP=6
EX_BUILD=7
EX_COPY=8

PROG=${0##*/}
WORKDIR=""

cleanup() {
    trap - 0 HUP INT QUIT PIPE TERM
    if [ -n "$WORKDIR" ] && [ -d "$WORKDIR" ]; then
        cd / 2>/dev/null || :
        rm -rf -- "$WORKDIR"
    fi
}

trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 131' QUIT
trap 'exit 141' PIPE
trap 'exit 143' TERM

die() {
    code=$1
    shift
    echo "$PROG: $*" >&2
    exit "$code"
}

[ $# -eq 1 ] || die "$EX_USAGE" "использование: $0 исходный_файл"

SRC=$1

if [ ! -f "$SRC" ] || [ ! -r "$SRC" ]; then
    die "$EX_NOFILE" "файл '$SRC' не найден или не читается"
fi

case $SRC in
    *.c)               TOOL=gcc ;;
    *.cpp|*.cc|*.cxx)  TOOL=g++ ;;
    *.tex)             TOOL=pdflatex ;;
    *) die "$EX_TYPE" "неподдерживаемое расширение (нужно .c, .cpp, .cc, .cxx или .tex)" ;;
esac

if ! command -v "$TOOL" >/dev/null 2>&1; then
    die "$EX_NOTOOL" "не найден '$TOOL', установите его"
fi

ORIG_DIR=$(pwd) || die "$EX_COPY" "не удалось определить текущий каталог"
SRC_DIR=$(CDPATH= cd -- "$(dirname -- "$SRC")" && pwd) \
    || die "$EX_NOFILE" "не удалось определить каталог исходного файла"
SRC_NAME=$(basename -- "$SRC")
SRC_ABS=$SRC_DIR/$SRC_NAME

OUT_NAME=$(sed -n 's|^[[:space:]]*[/%*][/%*]*[[:space:]]*Output:[[:space:]]*||p' "$SRC_ABS" \
           | sed -n '1s/[[:space:]*].*//p')

if [ -z "$OUT_NAME" ]; then
    die "$EX_NOOUT" "в '$SRC' не найден комментарий вида 'Output: имя_файла'"
fi

case $OUT_NAME in
    */*|.*|-*) die "$EX_NOOUT" "недопустимое имя результата '$OUT_NAME'" ;;
esac

if [ "$ORIG_DIR/$OUT_NAME" = "$SRC_ABS" ]; then
    die "$EX_NOOUT" "имя результата совпадает с именем исходного файла"
fi

WORKDIR=$(mktemp -d "${TMPDIR:-/tmp}/build.XXXXXX") \
    || die "$EX_MKTEMP" "не удалось создать временный каталог"
WORKDIR=$(CDPATH= cd -- "$WORKDIR" && pwd) \
    || die "$EX_MKTEMP" "не удалось определить путь к временному каталогу"

cd -- "$WORKDIR" || die "$EX_MKTEMP" "не удалось перейти во временный каталог"

case $TOOL in
    gcc|g++)
        BUILT=$WORKDIR/$OUT_NAME
        if ! TMPDIR="$WORKDIR" "$TOOL" -o "$BUILT" "$SRC_ABS"; then
            die "$EX_BUILD" "ошибка компиляции '$SRC'"
        fi
        ;;
    pdflatex)
        BASE=$(basename -- "$SRC_NAME" .tex)
        BUILT=$WORKDIR/$BASE.pdf
        if ! TMPDIR="$WORKDIR" TEXINPUTS="$SRC_DIR:${TEXINPUTS:-}" \
                pdflatex -interaction=nonstopmode -halt-on-error \
                -output-directory="$WORKDIR" "$SRC_ABS" \
                </dev/null >"$WORKDIR/pdflatex.out" 2>&1; then
            tail -n 30 "$WORKDIR/pdflatex.out" >&2
            die "$EX_BUILD" "ошибка сборки TeX-документа '$SRC'"
        fi
        ;;
esac

if [ ! -f "$BUILT" ]; then
    die "$EX_BUILD" "сборка завершилась, но результат '$BUILT' не создан"
fi

if ! cp -- "$BUILT" "$ORIG_DIR/$OUT_NAME"; then
    die "$EX_COPY" "не удалось записать '$ORIG_DIR/$OUT_NAME'"
fi

echo "Готово: $ORIG_DIR/$OUT_NAME"
exit 0
