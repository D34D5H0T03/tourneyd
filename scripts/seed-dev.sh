#!/bin/bash
# Fills the database with small but real CTF challenges and a few sample writeups.
#   usage: scripts/seed-dev.sh [-c config] [--no-test]
# Each challenge is generated from its flag and SELF-CHECKED: a solver reads only the generated
# files and must recover the flag, so a broken challenge stops the script.
# WARNING: this file contains every solution. If you publish the challenges, keep it private.
set -euo pipefail
export LC_ALL=C
cd "$(dirname "$0")/.."

CTL=${CTL:-./build/tourneyctl}
CTL_ARGS=()
WITH_TEST=1
while [ $# -gt 0 ]; do
    case $1 in
        -c)        [ $# -ge 2 ] || { echo "-c needs a file" >&2; exit 2; }; CTL_ARGS=(-c "$2"); shift 2 ;;
        --no-test) WITH_TEST=0; shift ;;
        -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
        *)         echo "usage: $0 [-c config] [--no-test]" >&2; exit 2 ;;
    esac
done

ctl() { "$CTL" "${CTL_ARGS[@]}" "$@"; }

[ -x "$CTL" ] || { echo "build the CLI first: make build/tourneyctl" >&2; exit 1; }
for tool in gcc xxd base64 od rev tr date factor sed; do
    command -v "$tool" >/dev/null || { echo "missing tool: $tool (for xxd: sudo apt install xxd)" >&2; exit 1; }
done

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FILES=public/files

# ---------- flags (this is the answer key) ----------
FLAG_TEST='flag{this_is_only_a_test}'
FLAG_LAYERS='flag{l4y3rs_up0n_l4y3rs_up0n_l4y3rs}'
FLAG_OBK='flag{x0r_w1th_4_s1ngl3_byt3_1s_n0t_s3cur3}'
FLAG_RSA='flag{sm4ll_pr1m3s_g1v3_sm4ll_s3cr3ts}'
FLAG_LOGS='flag{th3_4tt4ck3r_l3ft_4_tr41l_1n_th3_l0gs}'
FLAG_CHECK='flag{r3v3rs1ng_1s_just_und01ng_th3_m4th}'

# ---------- helpers ----------
verify() {   # slug expected actual
    if [ "$2" != "$3" ]; then echo "SELF-CHECK FAILED for $1" >&2; exit 1; fi
    echo "  self-check ok: $1"
}

register() { # slug title category points flag description-file
    local slug=$1 title=$2 category=$3 points=$4 flag=$5 desc=$6
    ctl challenge delete --slug "$slug" --yes >/dev/null 2>&1 || true     # idempotent: replace if present
    printf '%s' "$flag" | ctl challenge add --slug "$slug" --title "$title" --category "$category" \
        --points "$points" --desc-file "$desc" --flag-stdin
}

fresh_dir() { rm -rf "$FILES/$1"; mkdir -p "$FILES/$1"; echo "$FILES/$1"; }

# ---------- number theory for tiny-rsa (everything stays below 2^62, so bash's 64-bit math is enough) ----------
is_prime() {
    local n=$1 d
    if [ "$n" -lt 2 ]; then return 1; fi
    for (( d = 2; d * d <= n; d++ )); do
        if [ $(( n % d )) -eq 0 ]; then return 1; fi
    done
    return 0
}
next_prime() { local n=$1; until is_prime "$n"; do n=$(( n + 1 )); done; echo "$n"; }

modpow() {   # base exponent modulus
    local b=$(( $1 % $3 )) e=$2 m=$3 r=1
    while [ "$e" -gt 0 ]; do
        if [ $(( e & 1 )) -eq 1 ]; then r=$(( r * b % m )); fi
        b=$(( b * b % m ))
        e=$(( e >> 1 ))
    done
    echo "$r"
}

modinv() {   # inverse of a modulo m (extended Euclid)
    local r0=$2 r1=$1 t0=0 t1=1 q t
    while [ "$r1" -ne 0 ]; do
        q=$(( r0 / r1 ))
        t=$(( r0 - q * r1 )); r0=$r1; r1=$t
        t=$(( t0 - q * t1 )); t0=$t1; t1=$t
    done
    if [ "$t0" -lt 0 ]; then t0=$(( t0 + $2 )); fi
    echo "$t0"
}

# ============ layers ============
gen_layers() {   # reverse -> rot13 -> base64 -> hex
    printf '%s\n' "$FLAG_LAYERS" | rev | tr 'A-Za-z' 'N-ZA-Mn-za-m' | base64 -w0 | xxd -p | tr -d '\n' > "$1/layers.txt"
    echo >> "$1/layers.txt"
}
solve_layers() { tr -d '\n' < "$1/layers.txt" | xxd -r -p | base64 -d | tr 'A-Za-z' 'N-ZA-Mn-za-m' | rev; }

# ============ one-byte-key ============
OBK_KEY=90
gen_obk() {
    local b
    for b in $(printf '%s' "$FLAG_OBK" | od -An -tu1 -v); do printf '%02x' $(( b ^ OBK_KEY )); done > "$1/intercepted.hex"
    echo >> "$1/intercepted.hex"
}
solve_obk() {    # known plaintext: the message starts with "f" (102)
    local hex k i
    hex=$(tr -d '\n' < "$1/intercepted.hex")
    k=$(( 0x${hex:0:2} ^ 102 ))
    for (( i = 0; i < ${#hex}; i += 2 )); do
        printf "\\x$(printf '%02x' $(( 0x${hex:i:2} ^ k )))"
    done
}

# ============ tiny-rsa ============
RSA_E=65537
gen_rsa() {      # textbook RSA, one character at a time, with 15-bit primes
    local p q n b c=""
    p=$(next_prime 30011); q=$(next_prime 31013); n=$(( p * q ))
    for b in $(printf '%s' "$FLAG_RSA" | od -An -tu1 -v); do c+="$(modpow "$b" "$RSA_E" "$n") "; done
    { echo "n = $n"; echo "e = $RSA_E"; echo "c = ${c% }"; } > "$1/output.txt"
}
solve_rsa() {
    local n e c p q d m
    n=$(sed -n 's/^n = //p' "$1/output.txt"); e=$(sed -n 's/^e = //p' "$1/output.txt")
    c=$(sed -n 's/^c = //p' "$1/output.txt")
    read -r _ p q < <(factor "$n")
    d=$(modinv "$e" $(( (p - 1) * (q - 1) )))
    for m in $c; do printf "\\x$(printf '%02x' "$(modpow "$m" "$d" "$n")")"; done
}

# ============ suspicious-logs ============
gen_logs() {
    local out=$1/access.log i ip path ts epath estatus esize
    local base=1792000000
    local -a paths=( / /index.html /css/style.css /js/app.js /challenges.html /writeups.html /api/challenges /api/writeups )
    local -a evil=(
        "/admin 404 196"
        "/.env 404 196"
        "/backup.zip 404 196"
        "/wp-login.php 404 196"
        "/admin/export?token=$(printf '%s' "$FLAG_LOGS" | base64 -w0) 200 2048"
    )
    RANDOM=7                                   # seeding $RANDOM makes the log identical on every run
    : > "$out"
    for (( i = 0; i < 300; i++ )); do
        ts=$(date -u -d "@$(( base + i * 41 ))" '+%d/%b/%Y:%H:%M:%S +0000')
        ip="198.51.100.$(( RANDOM % 40 + 1 ))"
        path=${paths[RANDOM % ${#paths[@]}]}
        printf '%s - - [%s] "GET %s HTTP/1.1" 200 %d "-" "Mozilla/5.0"\n' "$ip" "$ts" "$path" $(( RANDOM % 4000 + 300 )) >> "$out"
        if [ "$i" -ge 150 ] && [ "$i" -lt $(( 150 + ${#evil[@]} )) ]; then
            read -r epath estatus esize <<< "${evil[i - 150]}"
            printf '%s - - [%s] "GET %s HTTP/1.1" %s %s "-" "curl/8.5.0"\n' 203.0.113.77 "$ts" "$epath" "$estatus" "$esize" >> "$out"
        fi
    done
}
solve_logs() {
    grep -v '^198\.51\.100\.' "$1/access.log" | grep ' 200 ' \
        | sed -n 's/.*token=\([^ ]*\) HTTP.*/\1/p' | tr -d '\n' | base64 -d
}

# ============ check-me ============
gen_check() {    # stored[i] = ((flag[i] + 3*i) & 255) ^ 92
    local flag=$FLAG_CHECK i b arr=""
    for (( i = 0; i < ${#flag}; i++ )); do
        b=$(printf '%d' "'${flag:i:1}")
        arr+="$(( ((b + i * 3) & 255) ^ 92 )),"
    done
    cat > "$TMP/checker.c" <<EOF
#include <stdio.h>
#include <string.h>

static const unsigned char want[] = { ${arr%,} };

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <input>\n", argv[0]); return 2; }
    size_t n = sizeof want;
    if (strlen(argv[1]) != n) { puts("Nope."); return 1; }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)argv[1][i];
        if ((unsigned char)(((c + i * 3) & 255) ^ 92) != want[i]) { puts("Nope."); return 1; }
    }
    puts("Correct! Submit it.");
    return 0;
}
EOF
    gcc -O1 -s -o "$1/checker" "$TMP/checker.c"
}
verify_check() {
    local good bad
    good=$("$1/checker" "$FLAG_CHECK" || true)
    bad=$("$1/checker" "flag{definitely_wrong}" || true)
    if [ "$good" != "Correct! Submit it." ] || [ "$bad" != "Nope." ]; then
        echo "SELF-CHECK FAILED for check-me" >&2; exit 1
    fi
    echo "  self-check ok: check-me"
}

# ============ build everything ============
echo "generating challenges"

dir=$(fresh_dir layers); gen_layers "$dir"; verify layers "$FLAG_LAYERS" "$(solve_layers "$dir")"
cat > "$TMP/layers.md" <<'EOF'
A teammate swore that nobody would ever read this file, so he "protected" the note by
encoding it four times. He did not write down the order.

Attachment: /files/layers/layers.txt

Hint: every layer is something you will meet again in other challenges.
EOF
register layers "Layers" misc 50 "$FLAG_LAYERS" "$TMP/layers.md"

dir=$(fresh_dir one-byte-key); gen_obk "$dir"; verify one-byte-key "$FLAG_OBK" "$(solve_obk "$dir")"
cat > "$TMP/obk.md" <<'EOF'
We intercepted a message from a rival team. It was "encrypted" with a single secret byte.
The message is the flag, and every flag on this platform starts with flag{ .

Attachment: /files/one-byte-key/intercepted.hex

Hint: 256 possible keys is not a lot, and you already know how the plaintext begins.
EOF
register one-byte-key "One Byte Key" crypto 100 "$FLAG_OBK" "$TMP/obk.md"

dir=$(fresh_dir tiny-rsa); gen_rsa "$dir"; verify tiny-rsa "$FLAG_RSA" "$(solve_rsa "$dir")"
cat > "$TMP/rsa.md" <<'EOF'
A student implemented RSA from scratch to protect a message. He was told that bigger is
better, but he picked the first primes that came to mind.

Each character of the message was encrypted separately, as its ASCII code. The public
values and the ciphertext are in the attachment.

Attachment: /files/tiny-rsa/output.txt

Hint: RSA is only as strong as the difficulty of factoring n. How hard is this n?
EOF
register tiny-rsa "Tiny RSA" crypto 150 "$FLAG_RSA" "$TMP/rsa.md"

dir=$(fresh_dir suspicious-logs); gen_logs "$dir"; verify suspicious-logs "$FLAG_LOGS" "$(solve_logs "$dir")"
cat > "$TMP/logs.md" <<'EOF'
Our web server was probed and then breached. Somebody walked away with a secret token.
The access log of that evening is attached. The flag is the secret hidden inside the token
the attacker took.

Attachment: /files/suspicious-logs/access.log

Hint: normal visitors come from one network. Attackers do not behave like normal visitors.
EOF
register suspicious-logs "Suspicious Logs" forensics 150 "$FLAG_LOGS" "$TMP/logs.md"

dir=$(fresh_dir check-me); gen_check "$dir"; verify_check "$dir"
cat > "$TMP/check.md" <<'EOF'
We found this program on the laptop of an engineer who no longer works here. It asks for
exactly one input and answers either "Correct!" or "Nope.". The correct input is the flag.

Attachment: /files/check-me/checker   (a Linux executable; run it as ./checker <input>)

Hint: you do not need to run it to understand it, and strings alone will not be enough.
EOF
register check-me "Check Me" reversing 200 "$FLAG_CHECK" "$TMP/check.md"

if [ "$WITH_TEST" -eq 1 ]; then
    printf 'This challenge exists to test the submission pipeline end to end.\nThe flag is: %s\n' "$FLAG_TEST" > "$TMP/test.md"
    register test-flag "Test Flag" testing 0 "$FLAG_TEST" "$TMP/test.md"
else
    ctl challenge delete --slug test-flag --yes >/dev/null 2>&1 || true
fi

# ============ writeups ============
echo "adding writeups"
cat > "$TMP/wu-layers.md" <<'EOF'
Walkthrough: Layers

The file holds one long line of hexadecimal digits. Every layer has to be removed in the
reverse order of how it was applied. The usual suspects:

  1. Hex      ->  xxd -r -p
  2. Base64   ->  base64 -d
  3. ROT13    ->  tr 'A-Za-z' 'N-ZA-Mn-za-m'
  4. Reversed ->  rev

As one pipeline:

  tr -d '\n' < layers.txt | xxd -r -p | base64 -d | tr 'A-Za-z' 'N-ZA-Mn-za-m' | rev

How to recognize a layer: hex uses only 0-9 and a-f and has an even length; base64 uses letters,
digits, + and /, often ends with =, and its length is a multiple of 4; ROT13 text still has
the right "shape" of words but wrong letters; reversed text reads backwards.

Lesson: encoding is not encryption. Nothing here needs a key.
EOF
ctl writeup put --slug layers-walkthrough --title "Walkthrough: Layers" --author tourneyd \
    --challenge layers --file "$TMP/wu-layers.md"

cat > "$TMP/wu-flags.md" <<'EOF'
How this platform stores flags

A challenge's flag is never stored. Only two values are kept per challenge:

  flag_salt  16 random bytes, different for every challenge
  flag_hash  SHA-256(flag_salt || flag), 32 bytes

When you submit a flag, the server repeats the computation with the stored salt and compares the
two hashes in constant time, so the comparison does not leak how many bytes matched.

Why salt: without it, two challenges with the same flag would have the same hash, and a
precomputed table of hashes of likely flags would work against every challenge at once.

Why a fast hash is acceptable here but not for passwords: flags are long and random, so
guessing them is hopeless even at billions of guesses per second. Human-chosen passwords are
short and predictable, so they need a deliberately slow function such as PBKDF2.

Every submission attempt is logged (challenge and result only, never the text you typed).
The same log is used to rate-limit guessing.
EOF
ctl writeup put --slug how-flags-are-stored --title "How this platform stores flags" --author tourneyd --file "$TMP/wu-flags.md"

cat > "$TMP/wu-ctftime.md" <<'EOF'
A link-out entry: writeups for many past events are collected on CTFtime. This entry shows how
a writeup that lives elsewhere is listed here with a link to the original.
EOF
ctl writeup put --slug ctf-writeups-around-the-web --title "Writeups around the web" \
    --author "various" --source-url "https://ctftime.org/writeups" --file "$TMP/wu-ctftime.md"

echo
ctl challenge list
ctl writeup list