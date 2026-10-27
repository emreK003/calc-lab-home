// Emre Kiyak 260AEM008
// https://github.com/emreK003/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: C man pages for strtod, strtoll, pow, opendir, readdir, mkdir, stat, freopen.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

// Used in the output file and folder names.
#define NAME       "emre"
#define LASTNAME   "kiyak"
#define STUDENT_ID "260AEM008"

// A number is either an integer or a float. We have to remember which one,
// because 7 / 2 is 3 but 7 / 2.0 is 3.5.
typedef struct {
    int is_float;
    long long i;
    double d;
} Value;

enum { NUM, PLUS, MINUS, STAR, SLASH, POW, LPAREN, RPAREN, END, BAD };

typedef struct {
    int type;
    long start;   // index of the first character of the token in the file
    Value val;    // only used when type == NUM
} Token;

// Only one file is parsed at a time, so the parser state is kept in globals
// instead of passing it to every function.
static char *src;          // the whole input file
static long src_len;
static long pos;           // where the tokenizer continues reading
static Token tok;          // the token the parser is looking at
static long err_pos = -1;  // index of the first error, -1 if no error yet

static void error_at(long p)
{
    // only the first error of a line is kept
    if (err_pos < 0)
        err_pos = p;
}

// -Tokenizer

// How positions work: the whole file is in one buffer and pos is an index
// into that buffer. pos is never reset at a new line, so the '\n' characters
// and comment lines are counted automatically. The error position printed is
// just token.start + 1, because positions in the task start at 1.
// The end of a line is also a token (END). Its start is the index of the
// '\n', or src_len if the last line has no '\n'. So "missing operand at end
// of line" gives the '\n' position or file length + 1 without extra code.
static void next_token(void)
{
    while (pos < src_len && (src[pos] == ' ' || src[pos] == '\t'))
        pos++;

    tok.start = pos;
    tok.val.is_float = 0;
    tok.val.i = 0;
    tok.val.d = 0;

    if (pos >= src_len || src[pos] == '\n') {
        tok.type = END;   // pos stays on the '\n', evaluate() skips it
        return;
    }

    char c = src[pos];

    if (isdigit((unsigned char)c)) {
        long end = pos;
        while (end < src_len && isdigit((unsigned char)src[end]))
            end++;
        // The '.' is only part of the number if a digit follows it. So in
        // "5." or "5.e3" the number is 5 and the next token is the '.',
        // which is an invalid character -> the error is at the '.'.
        // In "1.5e3" the number is 1.5 and the error is at the 'e'.
        if (end + 1 < src_len && src[end] == '.' &&
            isdigit((unsigned char)src[end + 1])) {
            tok.val.is_float = 1;
            end++;
            while (end < src_len && isdigit((unsigned char)src[end]))
                end++;
        }
        // strtod/strtoll need a '\0' at the end of the number. Instead of
        // copying the number, put a '\0' there for a moment and put the
        // old character back. (src has one extra byte, see read_file.)
        char saved = src[end];
        src[end] = '\0';
        if (tok.val.is_float)
            tok.val.d = strtod(src + pos, NULL);
        else
            tok.val.i = strtoll(src + pos, NULL, 10);
        src[end] = saved;

        tok.type = NUM;
        pos = end;
        return;
    }

    pos++;
    if (c == '+')
        tok.type = PLUS;
    else if (c == '-')
        tok.type = MINUS;
    else if (c == '/')
        tok.type = SLASH;
    else if (c == '(')
        tok.type = LPAREN;
    else if (c == ')')
        tok.type = RPAREN;
    else if (c == '*') {
        if (pos < src_len && src[pos] == '*') {
            tok.type = POW;
            pos++;
        } else {
            tok.type = STAR;
        }
    } else {
        // anything else, also '.' and a '#' that is not at the start of
        // the line. The parser never accepts BAD, so the error is here.
        tok.type = BAD;
    }
}

// -Arithmetic

// Integer power with "exponentiation by squaring". A simple loop would be
// too slow for something like 1 ** 1000000000. 0 ** 0 gives 1.
static long long int_power(long long base, long long exp)
{
    long long result = 1;
    while (exp > 0) {
        if (exp % 2 == 1)
            result *= base;
        exp /= 2;
        if (exp > 0)   // don't square once more after the last bit
            base *= base;
    }
    return result;
}

// Calculates a op b. op_pos is where the operator is and right_pos is where
// the right operand starts. They are needed for the error positions:
//  - division by zero      -> right_pos (the divisor is the problem)
//  - negative int exponent -> right_pos (start of the exponent)
//  - inf / nan float       -> op_pos (no operand is wrong alone, only the
//                             operation, e.g. 10.0 ** 400)
//
// Int or float: if one of the operands is a float, both are converted to
// double and the result is a float. Otherwise everything stays long long,
// so / truncates toward zero like in C.
static Value calculate(int op, Value a, Value b, long op_pos, long right_pos)
{
    Value r = {0, 0, 0};

    if (a.is_float || b.is_float) {
        double x = a.is_float ? a.d : (double)a.i;
        double y = b.is_float ? b.d : (double)b.i;
        r.is_float = 1;

        if (op == PLUS)
            r.d = x + y;
        else if (op == MINUS)
            r.d = x - y;
        else if (op == STAR)
            r.d = x * y;
        else if (op == SLASH) {
            // checked before dividing, so 1.5 / 0.0 is a division by zero
            // error and not an "infinite result" error
            if (y == 0) {
                error_at(right_pos);
                return r;
            }
            r.d = x / y;
        } else {
            r.d = pow(x, y);   // negative exponent is ok for floats
        }

        if (isinf(r.d) || isnan(r.d))
            error_at(op_pos);
        return r;
    }

    if (op == PLUS)
        r.i = a.i + b.i;
    else if (op == MINUS)
        r.i = a.i - b.i;
    else if (op == STAR)
        r.i = a.i * b.i;
    else if (op == SLASH) {
        if (b.i == 0) {
            error_at(right_pos);
            return r;
        }
        r.i = a.i / b.i;
    } else {
        if (b.i < 0) {
            error_at(right_pos);
            return r;
        }
        r.i = int_power(a.i, b.i);
    }
    return r;
}

// -Parser
// Recursive descent parser, one function per precedence level:
//   expr    = term { ('+' | '-') term }
//   term    = power { ('*' | '/') power }
//   power   = primary [ '**' power ]
//   primary = NUMBER | '(' expr ')'
// Precedence: each function only works with results of the next (stronger)
// level. In 3 + 2 * 5, expr calls term, and term already computes 2 * 5
// before expr does the +. Parentheses go back to expr, so they can change
// the order.
// + - * / are left-associative because expr and term use a loop:
// 7 - 2 - 1 is computed as (7 - 2) - 1.
// ** is right-associative because power() calls itself for the exponent,
// so 2 ** 3 ** 2 groups as 2 ** (3 ** 2) = 512.
// The values are calculated while parsing (no tree). If a function cannot
// continue, it calls error_at() with the start of the current token, and all
// callers return as soon as err_pos is set.

static Value expr(void);

static Value primary(void)
{
    Value v = {0, 0, 0};

    if (tok.type == NUM) {
        v = tok.val;
        next_token();
    } else if (tok.type == LPAREN) {
        next_token();
        v = expr();
        if (err_pos >= 0)
            return v;
        // missing ')': error at the token we got instead. At the end of the
        // line this is the END token, so the '\n' position.
        if (tok.type != RPAREN)
            error_at(tok.start);
        else
            next_token();
    } else {
        // An operand was expected but something else is here. This covers:
        // two operators in a row ("7 + * 3" -> second operator), a unary
        // minus ("-5" -> the '-'), missing operand at end of line ("3 *" ->
        // the '\n'), "()" -> the ')', and invalid characters.
        error_at(tok.start);
    }
    return v;
}

static Value power(void)
{
    Value base = primary();
    if (err_pos >= 0 || tok.type != POW)
        return base;

    long op_pos = tok.start;
    next_token();
    long exp_pos = tok.start;
    Value exponent = power();   // right-associative, see above
    if (err_pos >= 0)
        return base;
    return calculate(POW, base, exponent, op_pos, exp_pos);
}

static Value term(void)
{
    Value left = power();
    while (err_pos < 0 && (tok.type == STAR || tok.type == SLASH)) {
        int op = tok.type;
        long op_pos = tok.start;
        next_token();
        long right_pos = tok.start;
        Value right = power();
        if (err_pos >= 0)
            break;
        left = calculate(op, left, right, op_pos, right_pos);
    }
    return left;
}

static Value expr(void)
{
    Value left = term();
    while (err_pos < 0 && (tok.type == PLUS || tok.type == MINUS)) {
        int op = tok.type;
        long op_pos = tok.start;
        next_token();
        long right_pos = tok.start;
        Value right = term();
        if (err_pos >= 0)
            break;
        left = calculate(op, left, right, op_pos, right_pos);
    }
    return left;
}

// -Files

// Large files: the file is read once into one buffer, and the tokenizer
// works with indexes into it, so no line is ever copied. Results are
// written directly to the output file. Everything is one pass over the file.
static int read_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "calc: %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "calc: %s: not a file\n", path);
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "calc: %s: %s\n", path, strerror(errno));
        return -1;
    }
    src_len = (long)st.st_size;
    src = malloc(src_len + 1);   // +1 for the '\0' trick in next_token
    if (src == NULL || fread(src, 1, src_len, f) != (size_t)src_len) {
        fprintf(stderr, "calc: cannot read %s\n", path);
        free(src);
        fclose(f);
        return -1;
    }
    src[src_len] = '\0';
    fclose(f);
    return 0;
}

// Goes through the lines of src and writes one result per line to out.
// Returns -1 if everything was ok, otherwise the index of the first error.
static long evaluate(FILE *out)
{
    long line = 0;   // index where the current line starts

    while (line < src_len) {
        long p = line;
        while (p < src_len && (src[p] == ' ' || src[p] == '\t'))
            p++;

        if (p >= src_len)   // only spaces at the end of the file
            break;
        if (src[p] == '\n') {   // blank line
            line = p + 1;
            continue;
        }
        if (src[p] == '#') {    // comment line
            while (p < src_len && src[p] != '\n')
                p++;
            line = p + 1;
            continue;
        }

        pos = p;
        err_pos = -1;
        next_token();
        Value v = expr();

        // expr stops at the first token it cannot use. If that is not the
        // end of the line, it is an extra token, e.g. the extra ')' in
        // "(1 + 2))" or the "3" in "2 3".
        if (err_pos < 0 && tok.type != END)
            error_at(tok.start);
        if (err_pos >= 0)
            return err_pos;

        if (v.is_float)
            fprintf(out, "%.15g\n", v.d);
        else
            fprintf(out, "%lld\n", v.i);

        line = tok.start + 1;   // tok is END, skip its '\n'
    }
    return -1;
}

// Creates the folder, or does nothing if it already exists.
static int make_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;
    if (mkdir(path, 0777) != 0) {
        fprintf(stderr, "calc: cannot create folder %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

// Evaluates one input file into <out_dir>/<base>_<name>_<lastname>_<id>.txt
static int calc_file(const char *in_path, const char *out_dir, const char *base)
{
    // read the input first, so a missing file does not create an empty folder
    if (read_file(in_path) != 0)
        return -1;
    if (make_dir(out_dir) != 0) {
        free(src);
        return -1;
    }

    const char *suffix = "_" NAME "_" LASTNAME "_" STUDENT_ID ".txt";
    char *out_path = malloc(strlen(out_dir) + strlen(base) + strlen(suffix) + 2);
    if (out_path == NULL) {
        free(src);
        return -1;
    }
    sprintf(out_path, "%s/%s%s", out_dir, base, suffix);

    int result = 0;
    FILE *out = fopen(out_path, "w");
    if (out == NULL) {
        fprintf(stderr, "calc: cannot write %s: %s\n", out_path, strerror(errno));
        result = -1;
    } else {
        long err = evaluate(out);
        if (err >= 0) {
            // Rule 3: on an error the file has only "ERROR:<pos>".
            // freopen with "w" empties the file and throws away the results
            // that were already written.
            out = freopen(out_path, "w", out);
            if (out != NULL)
                fprintf(out, "ERROR:%ld\n", err + 1);
        }
        if (out == NULL || fclose(out) != 0) {
            fprintf(stderr, "calc: cannot write %s\n", out_path);
            result = -1;
        }
    }

    free(out_path);
    free(src);
    return result;
}

// "labs/task1.txt" -> "task1", "../in/" -> "in". The result must be freed.
static char *base_name(const char *path)
{
    char *copy = malloc(strlen(path) + 1);
    if (copy == NULL)
        return NULL;
    strcpy(copy, path);

    size_t n = strlen(copy);
    while (n > 1 && copy[n - 1] == '/')   // remove trailing slashes
        copy[--n] = '\0';
    if (n > 4 && strcmp(copy + n - 4, ".txt") == 0)
        copy[n - 4] = '\0';

    char *slash = strrchr(copy, '/');
    if (slash != NULL && slash[1] != '\0')
        memmove(copy, slash + 1, strlen(slash + 1) + 1);
    return copy;
}

// Evaluates every *.txt file directly inside dir. Subfolders and other
// files are skipped.
static int calc_dir(const char *dir, const char *out_dir)
{
    DIR *d = opendir(dir);
    if (d == NULL) {
        fprintf(stderr, "calc: %s: %s\n", dir, strerror(errno));
        return -1;
    }
    if (make_dir(out_dir) != 0) {
        closedir(d);
        return -1;
    }

    int result = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        size_t n = strlen(entry->d_name);
        if (n <= 4 || strcmp(entry->d_name + n - 4, ".txt") != 0)
            continue;

        char *path = malloc(strlen(dir) + n + 2);
        if (path == NULL) {
            result = -1;
            break;
        }
        sprintf(path, "%s/%s", dir, entry->d_name);

        // stat is needed to skip a subfolder whose name ends with .txt
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            char *base = base_name(entry->d_name);
            if (base == NULL || calc_file(path, out_dir, base) != 0)
                result = -1;   // continue with the other files
            free(base);
        }
        free(path);
    }
    closedir(d);
    return result;
}

static void usage(void)
{
    fprintf(stderr, "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
}

int main(int argc, char *argv[])
{
    const char *dir = NULL;
    const char *out_dir = NULL;
    const char *input = NULL;

    for (int i = 1; i < argc; i++) {
        int is_dir = strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0;
        int is_out = strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output-dir") == 0;

        if (is_dir || is_out) {
            if (i + 1 >= argc) {
                fprintf(stderr, "calc: %s needs a folder name\n", argv[i]);
                usage();
                return 1;
            }
            if (is_dir)
                dir = argv[i + 1];
            else
                out_dir = argv[i + 1];
            i++;   // the folder name is already used
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "calc: unknown option %s\n", argv[i]);
            usage();
            return 1;
        } else if (input == NULL) {
            input = argv[i];
        } else {
            fprintf(stderr, "calc: only one input file is allowed\n");
            usage();
            return 1;
        }
    }

    if ((dir == NULL && input == NULL) || (dir != NULL && input != NULL)) {
        usage();
        return 1;
    }

    // Without -o the output folder is <input_base>_<name>_<id>, where
    // input_base is the file name without .txt, or the folder name with -d.
    char *base = base_name(dir != NULL ? dir : input);
    if (base == NULL)
        return 1;
    char *default_dir = NULL;
    if (out_dir == NULL) {
        default_dir = malloc(strlen(base) + strlen("_" NAME "_" STUDENT_ID) + 1);
        if (default_dir == NULL) {
            free(base);
            return 1;
        }
        sprintf(default_dir, "%s_%s_%s", base, NAME, STUDENT_ID);
        out_dir = default_dir;
    }

    int result;
    if (dir != NULL)
        result = calc_dir(dir, out_dir);
    else
        result = calc_file(input, out_dir, base);

    free(default_dir);
    free(base);
    return result == 0 ? 0 : 1;
}