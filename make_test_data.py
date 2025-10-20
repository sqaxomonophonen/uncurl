import os,sys,struct

if len(sys.argv) != 3:
	sys.stdout.write("Usage: %s <input> <output>\n" % sys.argv[0])
	sys.exit(1)

do_close_input = False
input = None
if sys.argv[1] == "-":
	input = sys.stdin
else:
	input = open(sys.argv[1], "rb")
	do_close_input = True

do_close_output = False
output = None
if sys.argv[2] == "-":
	assert not sys.stdout.isatty(), "output is a TTY"
	output = sys.stdout.buffer
else:
	output = open(sys.argv[2], "wb")
	do_close_output = True

for b in input.read():
	output.write(struct.pack("BBB", b, (b&0xf)<<4, b>0 and 255 or 0))

if do_close_input: input.close()
if do_close_output: output.close()
