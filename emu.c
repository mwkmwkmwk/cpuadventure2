#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <termios.h>
#include <sys/random.h>

// #define DEBUG

struct termios orig_tio;

void init_term(void) {
	setvbuf(stdin, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);
        struct termios tio;
        tcgetattr(fileno(stdin), &tio);
	orig_tio = tio;
	cfmakeraw(&tio);
        tcsetattr(fileno(stdin), TCSANOW, &tio);
	printf("\e[H\e[J\e[29r\e[29;1H\e[s");
}

void fini_term(void) {
        struct termios tio;
        tcsetattr(fileno(stdin), TCSANOW, &orig_tio);
	printf("\e[0m\e[1r\e[u");
}

struct byte {
	int8_t bits[9];
};
struct byte memory_raw[531441];
static struct byte *const memory = memory_raw + 265720;

struct byte regs_raw[9];
static struct byte *const regs = regs_raw + 4;

struct byte sregs_raw[9];
static struct byte *const sregs = sregs_raw + 4;
#define PGT(i) sregs[i]
#define PSW sregs[0]
#define EPC sregs[2]
#define EPSW sregs[3]
#define EDATA sregs[4]

#define SF PSW.bits[0]
#define CF PSW.bits[1]
#define TL PSW.bits[2]
#define XM PSW.bits[3]
#define WM PSW.bits[4]
#define RM PSW.bits[5]
#define XP PSW.bits[6]
#define WP PSW.bits[7]
#define RP PSW.bits[8]

struct byte pc;

struct byte file_buffer[9841];
int file_size;

int loglevel = 0;

int bits2int(const int8_t *bits, int num) {
	int res = 0;
	for (int i = num - 1; i >= 0; i--) {
		res *= 3;
		res += bits[i];
	}
	return res;
}

int byte2int(const struct byte *byte) {
	return bits2int(byte->bits, 9);
}

struct byte int2byte(int val) {
	val %= 19683;
	struct byte res;
	for (int i = 0; i < 9; i++) {
		switch(val % 3) {
			case 0:
				res.bits[i] = 0;
				break;
			case 1:
			case -2:
				res.bits[i] = 1;
				val -= 1;
				break;
			case 2:
			case -1:
				res.bits[i] = -1;
				val += 1;
				break;
		}
		val /= 3;
	}
	return res;
}

void byte2str(char str[static 10], const struct byte *byte) {
	static const char tab[3] = "-0+";
	for (int i = 0; i < 9; i++)
		str[i] = tab[1+byte->bits[8-i]];
	str[9] = 0;
}

void dump_registers(void) {
	int i;
	char str[10];
	byte2str(str, &pc);
	printf("PC = %s\r\n", str);
	for (i = -4; i < 5; i++) {
		byte2str(str, &regs[i]);
		printf("R%d = %s\r\n", i, str);
	}
	static const char *const sr_names[] = {
		"SCRN",
		"SCRZ",
		"SCRP",
		"PGTN",
		"PSW",
		"PGTP",
		"EPC",
		"EPSW",
		"EDATA",
	};
	static const char *const psw_names[] = {
		"SF",
		"CF",
		"TL",
		"XM",
		"WM",
		"RM",
		"XP",
		"WP",
		"RP",
	};
	for (i = -4; i < 5; i++) {
		if (i != 0 && i != 3) {
			byte2str(str, &sregs[i]);
			printf("%s = %s\r\n", sr_names[4+i], str);
		} else {
			for (int j = 0; j < 9; j++) {
				printf("%s.%s = %c\r\n", sr_names[4+i], psw_names[j], "-0+"[1+sregs[i].bits[j]]);
			}
		}
	}
}

#if 0
void test_convert(void) {
	for (int i = -9841; i <= 9841; i++) {
		struct byte b = int2byte(i);
		int x = byte2int(&b);
		char s[10];
		byte2str(s, &b);
		printf("%5d %5d %s\r\n", i, x, s);
		assert (i == x);
	}
}
#endif

void exception(int code, const struct byte *data) {
	int ntl;
	if (TL == -1) {
		// TRIPLE FAULT
		char str[10];
		byte2str(str, data);
		fini_term();
		printf("TRIPLE FAULT (code %d, data %s)\r\n", code, str);
		dump_registers();
		printf("CPU HALTED\r\n");
		exit(127);
	} else if (TL) {
		// DOUBLE FAULT
		EDATA = int2byte(code);
		code = -1;
		ntl = -1;
	} else {
		EDATA = *data;
		ntl = 1;
	}
	EPC = pc;
	EPSW = PSW;
	PSW = (struct byte){0};
	pc = int2byte(code * 3);
	TL = ntl;
}

void syscall(const struct byte *opcode) {
	exception(1, opcode);
}

void priverr(const struct byte *opcode) {
	exception(2, opcode);
}

void illegal(const struct byte *opcode) {
	exception(3, opcode);
}

void divby0(const struct byte *val) {
	exception(4, val);
}

void page_fault(const struct byte *va, int mode) {
	exception(-3 + mode, va);
}

bool mmu(int *pa, const struct byte *va, int mode) {
	int acc_mode = PSW.bits[4 + mode];
	int pg_mode = PSW.bits[7 + mode];
	if (!pg_mode) {
		if (acc_mode) {
			page_fault(va, mode);
			return false;
		}
		*pa = byte2int(va);
		return true;
	} else {
		int pgt = byte2int(&PGT(pg_mode));
		int vi = bits2int(va->bits + 6, 3);
		struct byte pte = memory[pgt * 27 + vi];
		int access = pte.bits[1 + mode];
		if (access == 0) {
			page_fault(va, mode);
			return false;
		}
		if (access == -1 && acc_mode) {
			page_fault(va, mode);
			return false;
		}
		*pa = bits2int(pte.bits + 3, 6) * 729 + bits2int(va->bits, 6);
		return true;
	}
}

bool load(struct byte *data, const struct byte *addr, int mode) {
	int pa;
	if (!mmu(&pa, addr, mode))
		return 0;
	*data = memory[pa];
#ifdef DEBUG
	char str[10];
	char str2[10];
	byte2str(str, addr);
	byte2str(str2, data);
	if (mode != -1)
		fprintf(stderr, "LOAD %d %d %s -> %s\r\n", XM, mode, str, str2);
#endif
	return true;
}

bool store(const struct byte *data, const struct byte *addr, int mode) {
	int pa;
	if (!mmu(&pa, addr, mode))
		return 0;
#ifdef DEBUG
	char str[10];
	char str2[10];
	byte2str(str, addr);
	byte2str(str2, data);
	fprintf(stderr, "STORE %d %d %s <- %s\r\n", XM, mode, str, str2);
#endif
	memory[pa] = *data;
	return true;
}

void op_add(struct byte *dst, const struct byte *a, const struct byte *b, int addsub, int cmode) {
	int carry = 0;
	if (cmode == 1)
		carry = CF;
	int sign = 0;
	for (int i = 0; i < 9; i++) {
		int tmp = carry + a->bits[i] + b->bits[i] * addsub;
		if (tmp < -1) {
			tmp += 3;
			carry = -1;
		} else if (tmp > 1) {
			tmp -= 3;
			carry = 1;
		} else {
			carry = 0;
		}
		dst->bits[i] = tmp;
		if (tmp)
			sign = tmp;
	}
	if (cmode == -1) {
		carry += CF;
		if (carry < -1)
			carry += 3;
		if (carry > 1)
			carry -= 3;
	}
	CF = carry;
	if (carry)
		sign = carry;
	SF = sign;
}

void op_xor(struct byte *dst, const struct byte *a, const struct byte *b) {
	int sign = 0;
	static const int xor_tab[9] = {
		1, -1, 0,
		-1, 0, 1,
		0, 1, -1,
	};
	for (int i = 0; i < 9; i++) {
		dst->bits[i] = xor_tab[4 + a->bits[i] + b->bits[i] * 3];
		if (dst->bits[i])
			sign = dst->bits[i];
	}
	SF = sign;
	CF = 0;
}

void op_and(struct byte *dst, const struct byte *a, const struct byte *b) {
	int sign = 0;
	for (int i = 0; i < 9; i++) {
		dst->bits[i] = a->bits[i] * b->bits[i];
		if (dst->bits[i])
			sign = dst->bits[i];
	}
	SF = sign;
	CF = 0;
}

void op_neg(struct byte *dst, const struct byte *a) {
	int sign = 0;
	for (int i = 0; i < 9; i++) {
		dst->bits[i] = -a->bits[i];
		if (dst->bits[i])
			sign = dst->bits[i];
	}
	SF = sign;
	CF = 0;
}

void op_shl(struct byte *dst, const struct byte *a, int shcnt) {
	int sign = 0;
	for (int i = 0; i < 9; i++) {
		int bit = i - shcnt;
		if (bit < 0 || bit >= 9)
			dst->bits[i] = 0;
		else
			dst->bits[i] = a->bits[bit];
		if (dst->bits[i])
			sign = dst->bits[i];
	}
	SF = sign;
	CF = 0;
}

void op_mul(struct byte *dst, const struct byte *a, const struct byte *b) {
	int res = byte2int(a) * byte2int(b);
	if (res < 0)
		SF = -1;
	else if (res > 0)
		SF = 1;
	else
		SF = 0;
	CF = 0;
	*dst = int2byte(res);
}

bool op_div(struct byte *dst, const struct byte *a, const struct byte *b) {
	int ia = byte2int(a);
	int ib = byte2int(b);
	if (!ib) {
		divby0(a);
		return false;
	}
	int res = ia / ib;
	if (res < 0)
		SF = -1;
	else if (res > 0)
		SF = 1;
	else
		SF = 0;
	CF = 0;
	*dst = int2byte(res);
	return true;
}

bool op_mod(struct byte *dst, const struct byte *a, const struct byte *b) {
	int ia = byte2int(a);
	int ib = byte2int(b);
	if (!ib) {
		divby0(a);
		return false;
	}
	int res = ia % ib;
	if (res < 0)
		SF = -1;
	else if (res > 0)
		SF = 1;
	else
		SF = 0;
	CF = 0;
	*dst = int2byte(res);
	return true;
}

void op_rng(struct byte *dst) {
	int x;
	getrandom(&x, sizeof x, 0);
	x &= 0x3fffffff;
	*dst = int2byte(x);
}

bool eval_cond(int cond) {
	switch (cond) {
		case -4:
			return SF != -1;
		case -3:
			return SF != 0;
		case -2:
			return SF != 1;
		case -1:
			return !CF;
		case 0:
			return true;
		case 1:
			return CF;
		case 2:
			return SF == -1;
		case 3:
			return SF == 0;
		case 4:
			return SF == 1;
		default:
			abort();
	}
}

void add(struct byte *dst, const struct byte *a, const struct byte *b) {
	int carry = 0;
	for (int i = 0; i < 9; i++) {
		int tmp = carry + a->bits[i] + b->bits[i];
		if (tmp < -1) {
			tmp += 3;
			carry = -1;
		} else if (tmp > 1) {
			tmp -= 3;
			carry = 1;
		} else {
			carry = 0;
		}
		dst->bits[i] = tmp;
	}
}

void incdec(struct byte *res, const struct byte *src, int delta) {
	for (int i = 0; i < 9; i++) {
		int tmp = src->bits[i] + delta;
		if (tmp == 2) {
			tmp = -1;
		} else if (tmp == -2) {
			tmp = 1;
		} else {
			delta = 0;
		}
		res->bits[i] = tmp;
	}
}

void inc(struct byte *res, const struct byte *src) {
	incdec(res, src, 1);
}

void dec(struct byte *res, const struct byte *src) {
	incdec(res, src, -1);
}

_Noreturn void hypercall_exit(void) {
	int code = byte2int(&regs[1]);
	fini_term();
	exit(code);
}

char *load_str(struct byte *bptr) {
	int ptr = byte2int(bptr);
	int bufsz = 1024;
	int bufpos = 0;
	char *buffer = malloc(bufsz);
	if (!buffer)
		abort();
	while (1) {
		struct byte addr = int2byte(ptr);
		struct byte data;
		if (!load(&data, &addr, 1))
			return 0;
		int d = byte2int(&data);
		if (!d)
			break;
		if (d < 0)
			d += 19683;
		if (bufpos + 5 > bufsz) {
			bufsz *= 2;
			buffer = realloc(buffer, bufsz);
			if (!buffer)
				abort();
		}
		assert(d >= 0);
		assert(d < 0x10000);
		if (d < 0x80) {
			buffer[bufpos++] = d;
		} else if (d < 0x800) {
			buffer[bufpos++] = 0xc0 | d >> 6;
			buffer[bufpos++] = 0x80 | d & 0x3f;
		} else {
			buffer[bufpos++] = 0xe0 | d >> 12;
			buffer[bufpos++] = 0x80 | d >> 6 & 0x3f;
			buffer[bufpos++] = 0x80 | d & 0x3f;
		}
		ptr++;
		if (ptr > 9841)
			break;
	}
	buffer[bufpos] = 0;
	return buffer;
}

bool hypercall_log(void) {
	char *buffer = load_str(&regs[1]);
	if (!buffer)
		return false;
	int level = byte2int(&regs[2]);
	if (level < -1)
		level = -1;
	if (level > 3)
		level = 3;
	if (level >= loglevel) {
		static const char *const loglevels[] = {
			"\e[0;34m[DEBUG]",
			"\e[0m[INFO]",
			"\e[0;33m[WARNING]",
			"\e[0;31m[ERROR]",
			"\e[0;31;1m[FATAL]",
		};
		printf("\e[u%s %s\e[0m\r\n\e[s", loglevels[1 + level], buffer);
	}
	free(buffer);
	return true;
}

int fetch_nb_data(struct byte *ptr, FILE *f) {
	int pos = 0;
	int tmp = 0;
	int any = 0;
	while (pos < 9841) {
		int c = fgetc(f);
		switch (c) {
			case EOF:
				if (any)
					ptr[pos++] = int2byte(tmp);
				return pos;
			case '\r':
				break;
			case '\n':
				if (any)
					ptr[pos++] = int2byte(tmp);
				tmp = 0;
				any = 0;
				break;
			case '-':
				any = 1;
				tmp *= 3;
				tmp -= 1;
				break;
			case '0':
				any = 1;
				tmp *= 3;
				break;
			case '+':
				any = 1;
				tmp *= 3;
				tmp += 1;
				break;
			default:
				return -1;
		}
	}
	return pos;
}

bool hypercall_open_nb() {
	int res = -1;
	char *buffer = load_str(&regs[1]);
	if (!buffer)
		return false;

	file_size = 0;
	if (buffer[0] == '.' || strchr(buffer, '/'))
		goto out;
	FILE *f = fopen(buffer, "r");
	if (f) {
		res = fetch_nb_data(file_buffer, f);
		fclose(f);
	}
	if (res != -1)
		file_size = res;
out:
	regs[1] = int2byte(res);
	free(buffer);
	return true;
}

bool hypercall_open_txt() {
	int res = -1;
	char *buffer = load_str(&regs[1]);
	if (!buffer)
		return false;

	file_size = 0;
	if (buffer[0] == '.' || strchr(buffer, '/'))
		goto out;
	FILE *f = fopen(buffer, "r");
	if (!f)
		goto out;
	int pos = 0;
	while (pos < 9841) {
		int c = fgetc(f);
		if (c == EOF)
			break;
		file_buffer[pos++] = int2byte(c);
	}
	fclose(f);

	file_size = pos;
	res = pos;
out:
	regs[1] = int2byte(res);
	free(buffer);
	return true;
}

bool hypercall_read() {
	int ptr = byte2int(&regs[1]);
	int sz = byte2int(&regs[2]);
	int pos = byte2int(&regs[3]);
	int res = -1;
	if (pos + sz > file_size || pos < 0 || sz < 0)
		goto out;
	for (int i = 0; i < sz; i++) {
		struct byte addr = int2byte(ptr + i);
		if (!store(&file_buffer[pos + i], &addr, 0))
			return false;
	}
	res = 0;
out:
	regs[1] = int2byte(res);
	return true;
}

bool op_hypercall(int code) {
	switch (code) {
		case 0:
			hypercall_exit();
		case 1:
			return hypercall_log();
		case 2:
			return hypercall_open_nb();
		case 3:
			return hypercall_read();
		case 4:
			return hypercall_open_txt();
	}
	return true;
}

bool termcall_beep() {
	printf("\a");
	return true;
}

bool termcall_putc() {
	int x = byte2int(&regs[1]);
	int y = byte2int(&regs[2]);
	int c = byte2int(&regs[3]);
	int fg = bits2int(regs[4].bits + 0, 2);
	int bg = bits2int(regs[4].bits + 2, 2);
	int bold = regs[4].bits[4];
	x += 40;
	y += 13;
	if (x < 0 || x > 80)
		return true;
	if (y < 0 || y > 27)
		return true;
	x++;
	y++;
	printf("\e[%d;%dH", y, x);
	printf("\e[0m");
	if (bold)
		printf("\e[1m");
	static const char *const fgs[] = {
		"\e[30m",
		"\e[31m",
		"\e[32m",
		"\e[33m",
		"",
		"\e[34m",
		"\e[35m",
		"\e[36m",
		"\e[37m",
	};
	static const char *const bgs[] = {
		"\e[40m",
		"\e[41m",
		"\e[42m",
		"\e[43m",
		"",
		"\e[44m",
		"\e[45m",
		"\e[46m",
		"\e[47m",
	};
	if (fg)
		printf(fgs[4+fg]);
	if (bg)
		printf(bgs[4+bg]);
	if (c < 0)
		c += 9841;
	if (c < 0x80)
		putchar(c);
	else if (c < 0x800) {
		putchar(0xc0 | c >> 6);
		putchar(0x80 | c & 0x3f);
	} else {
		putchar(0xe0 | c >> 12);
		putchar(0x80 | c >> 6 & 0x3f);
		putchar(0x80 | c & 0x3f);
	}
	return true;
}

bool termcall_getc() {
	int x = byte2int(&regs[1]);
	int y = byte2int(&regs[2]);
	x += 40;
	y += 13;
	if (x >= 0 && x < 81 && y >= 0 && y < 28) {
		x++;
		y++;
		printf("\e[%d;%dH", y, x);
	} else {
		printf("\e[u");
	}
	int res;
	while (1) {
		int c = getchar();
		if (c == EOF) {
			fini_term();
			printf("EOF\r\n");
			exit(126);
		}
		if (c < 0x80) {
			res = c;
			break;
		} else if ((c & 0xe0) == 0xc0) {
			int x1 = getchar();
			if ((x1 & 0xc0) != 0x80) {
				printf("\a");
				continue;
			}
			res = (c & 0x1f) << 6 | (x1 & 0x3f);
			break;
		} else if ((c & 0xf0) == 0xe0) {
			int x1 = getchar();
			if ((x1 & 0xc0) != 0x80) {
				printf("\a");
				continue;
			}
			int x2 = getchar();
			if ((x2 & 0xc0) != 0x80) {
				printf("\a");
				continue;
			}
			res = (c & 0xf) << 12 | (x1 & 0x3f) << 6 | (x2 & 0x3f);
			break;
		} else {
			printf("\a");
			continue;
		}
	}
	regs[1] = int2byte(res);
	return true;
}

bool op_termcall(int code) {
	switch (code) {
		case -1:
			return termcall_beep();
		case 0:
			return termcall_putc();
		case 1:
			return termcall_getc();
	}
	return true;
}

_Noreturn void run(void) {
	while(1) {
		struct byte opcode;
		struct byte imm;
		struct byte new_pc;
		struct byte res;
		struct byte addr;
		inc(&new_pc, &pc);
#ifdef DEBUG
		//dump_registers();
#endif
		if (!load(&opcode, &pc, -1))
			continue;
		int ra = bits2int(opcode.bits + 0, 2);
		int rb = bits2int(opcode.bits + 2, 2);
		int rc = bits2int(opcode.bits + 4, 2);
		int op;
		switch (op = bits2int(opcode.bits + 6, 3)) {
		case -13:
			// has 9-bit immediate
			if (!load(&imm, &new_pc, -1))
				continue;
			inc(&new_pc, &new_pc);
			switch (rc) {
				case -4:
					// AND ra, rb, i9
					op_and(&res, &regs[rb], &imm);
					goto writeback;
				case -3:
					// ADD ra, rb, i9
					op_add(&res, &regs[rb], &imm, 1, 0);
					goto writeback;
				case -2:
					// XOR ra, rb, i9
					op_xor(&res, &regs[rb], &imm);
					goto writeback;
				case -1:
					// LD ra, [rb + i9]
					add(&addr, &regs[rb], &imm);
					if (!load(&res, &addr, 1))
						continue;
					goto writeback;
				case 0:
					// JAL<cc> ra, i9
					if (eval_cond(rb)) {
						res = new_pc;
						new_pc = imm;
						goto writeback;
					}
					break;
				case 1:
					// ST ra, [rb + i9]
					add(&addr, &regs[rb], &imm);
					if (!store(&regs[ra], &addr, 0))
						continue;
					break;
				case 2:
					// DIV ra, rb, i9
					if (!op_div(&res, &regs[rb], &imm))
						continue;
					goto writeback;
				case 3:
					// MUL ra, rb, i9
					op_mul(&res, &regs[rb], &imm);
					goto writeback;
				case 4:
					// MOD ra, rb, i9
					if (!op_mod(&res, &regs[rb], &imm))
						continue;
					goto writeback;
			}
			break;
		case -12:
			// AND ra, rb, rc
			op_and(&res, &regs[rb], &regs[rc]);
			goto writeback;
		case -11:
			// XOR ra, rb, rc
			op_xor(&res, &regs[rb], &regs[rc]);
			goto writeback;
		case -10:
			// DIV ra, rb, rc
			if (!op_div(&res, &regs[rb], &regs[rc]))
				continue;
			goto writeback;
		case -9:
			// MUL ra, rb, rc
			op_mul(&res, &regs[rb], &regs[rc]);
			goto writeback;
		case -8:
			// MOD ra, rb, rc
			if (!op_mod(&res, &regs[rb], &regs[rc]))
				continue;
			goto writeback;
		case -7:
			// SHL ra, rb, rc
			op_shl(&res, &regs[rb], byte2int(&regs[rc]));
			goto writeback;
		case -6:
			// SHR ra, rb, rc
			op_shl(&res, &regs[rb], -byte2int(&regs[rc]));
			goto writeback;
		case -5:
			illegal(&opcode);
			continue;
		case -4:
		case -3:
		case -2:
			// LD ra, [rb + rc[++/--]]
			add(&addr, &regs[rb], &regs[rc]);
			if (!load(&res, &addr, 1))
				continue;
			if (rc) {
				if (op == -4)
					dec(&regs[rc], &regs[rc]);
				else if (op == -2)
					inc(&regs[rc], &regs[rc]);
			}
			goto writeback;
		case -1:
			// JAL<cc> ra, rb
			if (eval_cond(rc)) {
				res = new_pc;
				new_pc = regs[rb];
				goto writeback;
			}
			break;
		case 0:
			switch (bits2int(opcode.bits + 4, 2)) {
				case -4:
				case -3:
				case -2:
					illegal(&opcode);
					continue;
				case -1:
					// GETF
					res = (struct byte){PSW.bits[4+rb]};
					goto writeback;
				case 0:
					if (bits2int(opcode.bits + 0, 4) == -27) {
						// ERET
						if (XM) {
							priverr(&opcode);
							continue;
						}
						PSW = EPSW;
						pc = EPC;
						continue;
					} else if (opcode.bits[3] == -1 && opcode.bits[2] == 1) {
						// RNG
						op_rng(&res);
						goto writeback;
					} else if (opcode.bits[3] == 1) {
						// SETF
						if (ra >= -2 && XM) {
							priverr(&opcode);
							continue;
						}
						PSW.bits[4 + ra] = opcode.bits[2];
					} else {
						illegal(&opcode);
						continue;
					}
					break;
				case 1:
					switch (opcode.bits[3]) {
						case -1:
							// TVC
							if (XM != -1) {
								priverr(&opcode);
								continue;
							}
							if (!op_termcall(bits2int(opcode.bits, 3)))
								continue;
							break;
						case 0:
							// SVC
							syscall(&opcode);
							continue;
						case 1:
							// HVC
							if (XM) {
								priverr(&opcode);
								continue;
							}
							if (!op_hypercall(bits2int(opcode.bits, 3)))
								continue;
							break;
					}
					break;
				case 2:
					// S2R ra, rb
					if (XM) {
						priverr(&opcode);
						continue;
					}
					res = sregs[rb];
					goto writeback;
				case 3:
					// NEG ra, rb
					op_neg(&res, &regs[rb]);
					goto writeback;
				case 4:
					// R2S ra, rb
					if (XM) {
						priverr(&opcode);
						continue;
					}
					sregs[ra] = regs[rb];
					break;
			}
			break;
		case 1:
			illegal(&opcode);
			continue;
		case 2:
		case 3:
		case 4:
			// ST ra, [rb + rc[++/--]]
			add(&addr, &regs[rb], &regs[rc]);
			if (!store(&regs[ra], &addr, 0))
				continue;
			if (rc) {
				if (op == 2)
					dec(&regs[rc], &regs[rc]);
				else if (op == 4)
					inc(&regs[rc], &regs[rc]);
			}
			break;
		case 5:
			// SUBX ra, rb, rc
		case 6:
			// SUB ra, rb, rc
		case 7:
			// SUBC ra, rb, rc
		case 11:
			// ADDX ra, rb, rc
		case 12:
			// ADD ra, rb, rc
		case 13:
			// ADDC ra, rb, rc
			op_add(&res, &regs[rb], &regs[rc], opcode.bits[7], opcode.bits[6]);
			goto writeback;
		case 8:
		case 9:
		case 10:
			// SHL ra, rb, i3
			op_shl(&res, &regs[rb], bits2int(opcode.bits + 4, 3));
			goto writeback;
		writeback:
			if (ra)
				regs[ra] = res;
			break;
		}
		pc = new_pc;
	}
}

int main(int argc, char **argv) {
	const char *fname = "game.nb";
	if (argc >= 2)
		fname = argv[1];
	FILE *f = fopen(fname, "r");
	if (!f) {
		printf("No ROM.\r\n");
		return 1;
	}
	if (fetch_nb_data(memory, f) == -1) {
		printf("Broken ROM.\r\n");
		return 1;
	}
	init_term();
	run();
}
