#!/usr/bin/env python3
import sys
import datetime


def panic(*args):
    print(*args)
    exit(1)


user = None
expire = None
for line in sys.argv[1].split("\n"):
    if len(line) == 0:
        continue
    elms = line.split("=")
    if len(elms) != 2:
        panic("illformed line:", line)

    key, value = elms
    match key:
        case "user":
            user = value
            pass
        case "expire":
            expire = value
            pass
        case _:
            panic("unknown key:", line)

# user check
if not user:
    panic("no user key")

if not user in ["origin"]:
    panic("unallowed user:", user)

# expire check
if not expire:
    panic("no expire key")

try:
    expire = datetime.datetime.strptime(expire, "%Y%m%d")
except:
    panic("invalid expire date format")

now = datetime.datetime.now()
if now >= expire:
    panic("expired:", expire)

print("verified user", user)
exit(0)
