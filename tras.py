import sys
import string
from attr import attrs, attrib


@attrs
class Immediate:
    def encode(self, n=9):
        return int2bits(self.eval(), n)


@attrs
class Identifier(Immediate):
    val = attrib()

    def eval(self):
        return labels[self.val]


@attrs
class Negate(Immediate):
    sub = attrib()

    def eval(self):
        return -self.sub.eval()


@attrs
class Sum(Immediate):
    a = attrib()
    b = attrib()

    def eval(self):
        return self.a.eval() + self.b.eval()


@attrs
class Number(Immediate):
    val = attrib()

    def eval(self):
        return self.val


@attrs
class Memory:
    base = attrib()
    offs = attrib()
    inc = attrib()


@attrs
class Register:
    val = attrib()

    def encode(self):
        assert self.val in range(-4, 5)
        return int2bits(self.val, 2)


@attrs
class Flag:
    val = attrib()


@attrs
class SpecReg:
    val = attrib()


@attrs
class String(Immediate):
    val = attrib()

    def eval(self):
        return stringpool[self.val]


@attrs
class Bits:
    val = attrib()

    def encode(self, n=9):
        assert n == len(self.val)
        return self.val

    def eval(self):
        res = 0
        for x in self.val:
            res *= 3
            res += {
                '-': -1,
                '0': 0,
                '+': +1,
            }[x]
        return res

@attrs
class Plus:
    pass

@attrs
class PlusPlus:
    pass

@attrs
class Minus:
    pass

@attrs
class MinusMinus:
    pass

@attrs
class Comma:
    pass

@attrs
class Colon:
    pass

@attrs
class LBracket:
    pass

@attrs
class RBracket:
    pass


def int2bits(x, n=9):
    res = ''
    for i in range(n):
        if x % 3 == 0:
            res = '0' + res
        elif x % 3 == 1:
            res = '+' + res
            x -= 1
        else:
            res = '-' + res
            x += 1
        x //= 3
    assert x == 0
    return res

SPECS = {
    'SF': Flag('--'),
    'CF': Flag('-0'),
    'TL': Flag('-+'),
    'XM': Flag('0-'),
    'WM': Flag('00'),
    'RM': Flag('0+'),
    'XP': Flag('+-'),
    'WP': Flag('+0'),
    'RP': Flag('++'),
    'SCRN': SpecReg('--'),
    'SCRZ': SpecReg('-0'),
    'SCRP': SpecReg('-+'),
    'PGTN': SpecReg('0-'),
    'PSW': SpecReg('00'),
    'PGTP': SpecReg('0+'),
    'EPC': SpecReg('+-'),
    'EPSW': SpecReg('+0'),
    'EDATA': SpecReg('++'),
}

def tokenize(l):
    pos = 0
    while pos < len(l):
        if l[pos].isspace():
            pos += 1
        elif l[pos] == '#' and l[pos+1] == ' ':
            return
        elif l[pos].isalpha() or l[pos] == '_':
            opos = pos
            while pos < len(l) and (l[pos].isalpha() or l[pos].isdigit() or l[pos] == '_'):
                pos += 1
            yield Identifier(l[opos:pos])
        elif l[pos].isdigit():
            opos = pos
            while pos < len(l) and l[pos].isdigit():
                pos += 1
            yield Number(int(l[opos:pos]))
        elif l[pos] == '#':
            pos += 1
            res = ''
            while pos < len(l) and l[pos] in '-0+':
                res += l[pos]
                pos += 1
            yield Bits(res)
        elif l[pos] == '@':
            pos += 1
            opos = pos
            if l[pos].isalpha():
                opos = pos
                while pos < len(l) and (l[pos].isalpha() or l[pos].isdigit() or l[pos] == '_'):
                    pos += 1
                yield SPECS[l[opos:pos]]
            else:
                if l[pos] == '-':
                    pos += 1
                while pos < len(l) and l[pos].isdigit():
                    pos += 1
                if opos == pos:
                    raise ValueError(l)
                yield Register(int(l[opos:pos]))
        elif l[pos] == '-':
            pos += 1
            if l[pos] == '-':
                pos += 1
                yield MinusMinus()
            else:
                yield Minus()
        elif l[pos] == '+':
            pos += 1
            if l[pos] == '+':
                pos += 1
                yield PlusPlus()
            else:
                yield Plus()
        elif l[pos] == ',':
            pos += 1
            yield Comma()
        elif l[pos] == ':':
            pos += 1
            yield Colon()
        elif l[pos] == '[':
            pos += 1
            yield LBracket()
        elif l[pos] == ']':
            pos += 1
            yield RBracket()
        elif l[pos] == '"':
            pos += 1
            res = ''
            while True:
                if l[pos] == '"':
                    pos += 1
                    break
                elif l[pos] == '\\':
                    pos += 1
                    res += l[pos]
                    pos += 1
                else:
                    res += l[pos]
                    pos += 1
            yield String(res)
        elif l[pos] == '\'':
            pos += 1
            yield Number(ord(l[pos]))
            pos += 1
            assert l[pos] == '\''
            pos += 1
        else:
            raise ValueError(l)


def parse_arg(tokens):
    if isinstance(tokens[0], LBracket) and isinstance(tokens[-1], RBracket):
        tokens = tokens[1:-1]
        if len(tokens) == 2 and isreg(tokens[0]) and isinstance(tokens[1], (PlusPlus, MinusMinus)):
            inc = '+'
            if isinstance(tokens[1], MinusMinus):
                inc = '-'
            return Memory(None, tokens[0], inc)
        elif len(tokens) >= 3 and isinstance(tokens[-2], Plus) and isreg(tokens[-1]):
            return Memory(tokens[-1], parse_arg(tokens[:-2]), '0')
        elif len(tokens) >= 3 and isinstance(tokens[1], Plus) and isreg(tokens[0]):
            return Memory(tokens[0], parse_arg(tokens[2:]), '0')
        else:
            return Memory(None, parse_arg(tokens), '0')
    if len(tokens) == 2 and isinstance(tokens[0], Minus) and isimm(tokens[1]):
        return Negate(tokens[1])
    if len(tokens) == 3 and isimm(tokens[0]) and isinstance(tokens[1], Plus) and isimm(tokens[2]):
        return Sum(tokens[0], tokens[2])
    if len(tokens) == 3 and isimm(tokens[0]) and isinstance(tokens[1], Minus) and isimm(tokens[2]):
        return Sum(tokens[0], Negate(tokens[2]))
    arg, = tokens
    return arg


def parse_args(l):
    if not l:
        return []
    args = []
    pos = 0
    opos = 0
    while pos < len(l):
        if isinstance(l[pos], Comma):
            args.append(parse_arg(l[opos:pos]))
            opos = pos + 1
        pos += 1
    args.append(parse_arg(l[opos:pos]))
    return args

org = 0
out = []
labels = {}
stringpool = {}

def define(label, value):
    assert isinstance(label, Identifier)
    label = label.val
    if label in labels:
        raise ValueError(f'redefined label {label}')
    labels[label] = value

def emit(thing):
    if isinstance(thing, String):
        stringpool[thing.val] = None
    out.append(thing)

def isreg(x):
    return isinstance(x, Register)

def isimm(x):
    return isinstance(x, (Immediate, Bits))

SYSCALL_OPS = {
    'TVC': '0000+-',
    'SVC': '0000+0',
    'HVC': '0000++',
}

JUMP_OPS = {
    'JGE': '--',
    'JNZ': '-0',
    'JLE': '-+',
    'JNC': '0-',
    'J':   '00',
    'JC':  '0+',
    'JL':  '+-',
    'JZ':  '+0',
    'JG':  '++',
}

LDST_OPS = {
    'LD': '-',
    'ST': '+',
}

RRR_OPS = {
    'AND':  '--0',
    'XOR':  '--+',
    'DIV':  '-0-',
    'MUL':  '-00',
    'MOD':  '-0+',
    'SHL':  '-+-',
    'SHR':  '-+0',
    'SUBX': '+--',
    'SUB':  '+-0',
    'SUBC': '+-+',
    'ADDX': '++-',
    'ADD':  '++0',
    'ADDC': '+++',
}

RRI_OPS = {
    'AND': '-----',
    'ADD': '----0',
    'XOR': '----+',
    'DIV': '---+-',
    'MUL': '---+0',
    'MOD': '---++',
}

verbose = 0
if sys.argv[1] == '-v':
    verbose = 1
    del sys.argv[1]

with open(sys.argv[1]) as f:
    for l in f:
        try:
            l = list(tokenize(l))
            if len(l) >= 2 and isinstance(l[0], Identifier) and isinstance(l[1], Colon):
                define(l[0], len(out) + org)
                l = l[2:]
            if not l:
                continue
            if not isinstance(l[0], Identifier):
                raise ValueError(l[0])
            op = l[0].val
            args = parse_args(l[1:])
            if op == 'MOV' and len(args) == 2 and isreg(args[0]) and isimm(args[1]):
                emit('----000' + args[0].encode())
                emit(args[1])
            elif op in RRI_OPS and len(args) == 2 and isreg(args[0]) and isimm(args[1]):
                emit(RRI_OPS[op] + args[0].encode() * 2)
                emit(args[1])
            elif op in RRI_OPS and len(args) == 3 and isreg(args[0]) and isreg(args[1]) and isimm(args[2]):
                emit(RRI_OPS[op] + args[1].encode() + args[0].encode())
                emit(args[2])
            elif op == 'CMP' and len(args) == 2 and isreg(args[0]) and isimm(args[1]):
                emit('----0' + args[0].encode() + '00')
                emit(Negate(args[1]))
            elif op == 'SUB' and len(args) == 2 and isreg(args[0]) and isimm(args[1]):
                emit('----0' + args[0].encode() * 2)
                emit(Negate(args[1]))
            elif op in {'SHL', 'SHR'} and len(args) == 2 and isreg(args[0]) and isimm(args[1]):
                shcnt = args[1]
                if op == 'SHR':
                    shcnt = Negate(shcnt)
                emit('+0' + shcnt.encode(3) + args[0].encode() * 2)
            elif op in {'SHL', 'SHR'} and len(args) == 3 and isreg(args[0]) and isreg(args[1]) and isimm(args[2]):
                shcnt = args[2]
                if op == 'SHR':
                    shcnt = Negate(shcnt)
                emit('+0' + shcnt.encode(3) + args[1].encode() + args[0].encode())
            elif op == 'MOV' and len(args) == 2 and isreg(args[0]) and isreg(args[1]):
                emit('++000' + args[1].encode() + args[0].encode())
            elif op == 'NEG' and len(args) == 2 and isreg(args[0]) and isreg(args[1]):
                emit('000+0' + args[1].encode() + args[0].encode())
            elif op == 'CMP' and len(args) == 2 and isreg(args[0]) and isreg(args[1]):
                emit('+-0' + args[1].encode() + args[0].encode() + '00')
            elif op in RRR_OPS and len(args) == 2 and isreg(args[0]) and isreg(args[1]):
                emit(RRR_OPS[op] + args[1].encode() + args[0].encode() * 2)
            elif op in RRR_OPS and len(args) == 3 and isreg(args[0]) and isreg(args[1]) and isreg(args[2]):
                emit(RRR_OPS[op] + args[2].encode() + args[1].encode() + args[0].encode())
            elif op in SYSCALL_OPS and len(args) == 1 and isimm(args[0]):
                emit(SYSCALL_OPS[op] + args[0].encode(3))
            elif op in JUMP_OPS and len(args) == 1 and isimm(args[0]):
                emit('---00' + JUMP_OPS[op] + '00')
                emit(args[0])
            elif op in JUMP_OPS and len(args) == 2 and isreg(args[0]) and isimm(args[1]):
                emit('---00' + JUMP_OPS[op] + args[0].encode())
                emit(args[1])
            elif op in JUMP_OPS and len(args) == 1 and isreg(args[0]):
                emit('00-' + JUMP_OPS[op] + args[0].encode() + '00')
            elif op in JUMP_OPS and len(args) == 2 and isreg(args[0]) and isreg(args[1]):
                emit('00-' + JUMP_OPS[op] + args[1].encode() + args[0].encode())
            elif op in LDST_OPS:
                assert len(args) == 2
                assert isreg(args[0])
                assert isinstance(args[1], Memory)
                base = args[1].base
                offs = args[1].offs
                inc = args[1].inc
                if base is None:
                    base = Register(0)
                if isimm(offs):
                    emit('---0' + LDST_OPS[op] + base.encode() + args[0].encode())
                    emit(offs)
                elif isinstance(offs, Register):
                    emit('0' + LDST_OPS[op] + inc + offs.encode() + base.encode() + args[0].encode())
                else:
                    raise ValueError(op, args)
            elif op == 'GETF':
                assert len(args) == 2
                assert isreg(args[0])
                assert isinstance(args[1], Flag)
                emit('0000-' + args[1].val + args[0].encode())
            elif op == 'SETF':
                assert len(args) == 2
                assert isinstance(args[0], Flag)
                assert isimm(args[1])
                emit('00000+' + args[1].encode(1) + args[0].val)
            elif op == 'ERET':
                assert len(args) == 0
                emit('00000-000')
            elif op == 'RNG':
                assert len(args) == 1
                assert isreg(args[0])
                emit('00000-+' + args[0].encode())
            elif op == 'R2S':
                assert len(args) == 2
                assert isinstance(args[0], SpecReg)
                assert isreg(args[1])
                emit('000++' + args[1].encode() + args[0].val)
            elif op == 'S2R':
                assert len(args) == 2
                assert isreg(args[0])
                assert isinstance(args[1], SpecReg)
                emit('000+-' + args[1].val + args[0].encode())
            elif op == 'STRING' and len(args) == 1 and isinstance(args[0], String):
                for x in args[0].val:
                    emit(int2bits(ord(x)))
                emit(int2bits(0))
            elif op == 'BYTE':
                for x in args:
                    assert isimm(x)
                    emit(x)
            elif op == 'BYTES':
                assert len(args) == 1
                assert isinstance(args[0], Immediate)
                for _ in range(args[0].eval()):
                    emit(int2bits(0))
            elif op == 'PGALIGN':
                assert len(args) == 0
                while len(out) % 729 != 365:
                    emit(int2bits(0))
            elif op == 'STRINGPOOL':
                for s in sorted(stringpool):
                    if stringpool[s] is None:
                        stringpool[s] = len(out)
                        for x in s:
                            emit(int2bits(ord(x)))
                        emit(int2bits(0))
            elif op == 'ORG' and len(args) == 1 and isimm(args[0]):
                org = args[0].eval()
            elif op == 'CONST' and len(args) == 2 and isinstance(args[0], Identifier) and isimm(args[1]):
                define(args[0], args[1].eval())
            else:
                raise ValueError(op, args)
                pass
        except:
            print(l)
            raise


with open(sys.argv[2], 'w') as f:
    for x in out:
        if not isinstance(x, str):
            x = x.encode()
        assert all(y in '-0+' for y in x)
        assert len(x) == 9
        f.write(x + '\n')

if verbose:
    for l, v in labels.items():
        print(f'{int2bits(v)} {l} {v}')
