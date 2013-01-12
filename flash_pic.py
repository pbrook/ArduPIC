#! /usr/bin/env python
# Flash image onto a PIC16F54 via ArduPick

import sys
import io
import serial
import argparse

def verbose(s):
    return
    print s

def add_data(data, s, count):
    for n in xrange(0, count):
        data.append(int(s[n * 2:n * 2 + 2], 16))


def wait_done(ser):
    while True:
        s = ser.readline()
        print s,
        if s[0] == '.':
            return
        elif (s[0] == '!') or (s[0] == '#'):
            return

def program_word(ser, val):
    ser.write("%04x\n" % val)
    wait_done(ser)

delay_cmd = 1
delay_prog = 2000
delay_discharge = 10
delay_erase = 10000

class ArduPIC(object):
    def __init__(self, filename, baud):
        self.ser = serial.Serial(filename, baud)
        self.in_cmd = False

    def init(self):
        self.out("V00\n")
        s = self.ser.readline()
        if s != "00\n":
            raise Exception("Bad ArduPIC version")
        self.out_ack("T.%x" % delay_cmd);
        self.out_ack("Tg%x" % delay_prog);
        self.out_ack("Th%x" % delay_discharge);
        self.out_ack("Ti%x" % delay_erase);

    def out(self, s):
        if s[-1] == '\n':
            v = s[:-1] + "\\n"
        else:
            v = s
        verbose("Wrote '%s'" % v)
        self.ser.write(s);

    def out_ack(self, s = ""):
        self.out(s + '\n');
        self.ack();

    def ack(self):
        self.in_cmd = False
        s = '#'
        while s[0] == '#':
            s = self.ser.readline()
            verbose("Got '%s'" % s[:-1])
        if s != "\n":
            raise Exception("Programming failed%s" % s[:-1])

    def cmd_byte(self, cmd, delay = '.'):
        if not self.in_cmd:
            self.out('C')
        self.in_cmd = True
        self.out("*%02x%s" % (cmd, delay))

    def write_flash(self, data, readonly):
        data0 = (data << 1) & 0xfe
        data1 = (data >> 7) & 0x7f
        if not readonly:
            # Load program word
            self.cmd_byte(0x2)
            self.out("%02x%02x." % (data0, data1))
            # Begin programming
            self.cmd_byte(0x8, 'g')
            # End programming
            self.cmd_byte(0xe, 'h')
        # Verify (read program word)
        self.cmd_byte(0x4)
        self.out("=%02xfe=%02x7f." % (data0, data1))
        # Increment address
        self.cmd_byte(0x6)
        self.out_ack()

    def program(self, control, data, readonly):
        # Attach to device (reset and apply Vpp)
        self.out_ack("A");
        if not readonly:
            # Mass erase
            self.cmd_byte(0x9, 'i')
            self.out_ack()
        # Write control word
        self.write_flash(control, readonly)
        for i in xrange(0, len(data), 2):
            self.write_flash(data[i] | (data[i + 1] << 8), readonly)
        # Disconnect from device
        self.out_ack("D")

    def close(self):
        self.ser.close()

ap = argparse.ArgumentParser(description="PIC programmer")
ap.add_argument('-v', '--verify', action='store_true')
ap.add_argument('-r', '--read', action='store_true')
ap.add_argument('file', type=argparse.FileType("rt"), help="HEX image file")
ap.add_argument('-p', '--cpu', default="16f54")
ap.add_argument('-f', '--port', default='/dev/ttyACM0')
ap.add_argument('-b', '--baud', type=int, default=9600)
args = ap.parse_args()

def user_fail(s):
    print s
    exit(1)

if args.read:
    user_fail("--read not implemented")

cpu = args.cpu
if cpu[0:3] == "pic":
    cpu = cpu[3:]
elif cpu[0] == 'p':
    cpu=cpu[1:]

if cpu != "16f54":
    user_fail("Unsupported cpu")

# Read .hex file
# We currently only handle a single contiguous region at address zero,
f = args.file
next_addr = 0;
control_word = None
data = []
for l in f:
    if l[0] != ':':
        raise Exception("Expected ':'")
    count = int(l[1:3], 16)
    addr = int(l[3:7], 16)
    cmd = int(l[7:9], 16)
    if count * 2 + 12 != len(l):
        raise Exception("Incorrect line length")
    if cmd == 1:
        break;
    elif cmd == 4:
        if l[9:13] != "0000":
            raise Exception("Bad high address")
    elif cmd == 0:
        if addr == 0x1ffe:
            control_word = int(l[9:11], 16) | (int(l[11:13], 16) << 8)
        elif addr == next_addr:
            add_data(data, l[9:], count)
            next_addr += count
        else:
            raise Exception("Discontiguous address")

f.close()
if control_word is None:
    raise Exception("CONTROL word not set")

pic = ArduPIC(args.port, args.baud)
pic.init()
pic.program(control_word, data, args.verify)
pic.close()
print "Done"
