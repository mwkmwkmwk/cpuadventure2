import socket, termios, threading, tty, os, sys, time

addr = '127.0.0.1'
if len(sys.argv) > 1:
    addr = sys.argv[1]

conn = socket.create_connection((addr, 1337))

attr = termios.tcgetattr(0)

tty.setraw(0)

term = open('/dev/tty', 'rb+', buffering=0)

def thr():
    while True:
        d = conn.recv(1024)
        if not d:
            term.write(b'\x1b[r\x1b[u')
            termios.tcsetattr(0, termios.TCSAFLUSH, attr)
            os._exit(0)
        term.write(d)

threading.Thread(target=thr).start()

def slurp_nb(fname):
    res = ''
    with open(fname) as f:
        for l in f:
            n = 0
            for x in l.strip():
                n *= 3
                n += {'-': -1, '0': 0, '+': 1}[x]
            if n < 0:
                n += 3 ** 9
            res += chr(n)
    return res

def send_nb(data):
    conn.sendall(data.encode())

while True:
    x = term.read(1)
    if x == b'\x03':
        term.write(b'\x1b[r\x1b[u')
        termios.tcsetattr(0, termios.TCSAFLUSH, attr)
        os._exit(0)
    elif x == b'\x0b':
        input_buf = 2372
        gets_end = 1056
        bksp = b'\x08' * (input_buf - gets_end)
        conn.sendall(bksp)
        send_nb(slurp_nb('shc1.nb'))
        conn.sendall(b'\r')
        s2 = slurp_nb('shc2.nb')
        s3 = slurp_nb('shc3.nb')
        s4 = slurp_nb('shc4.nb')
        data = s2 + s3 + s4
        LEN = (3 ** 9 - 1) // 2
        while len(data) < LEN:
            data += '\0'
        assert len(data) == LEN
        send_nb(data)
    else:
        conn.sendall(x)
