#!/usr/bin/env python3
"""Minimal NUT client for testing UPS Watcher (or any upsd).

  nutclient.py HOST                 # like `upsc ups@HOST`: LIST UPS + LIST VAR
  nutclient.py HOST status          # just ups.status, once a second
  nutclient.py HOST raw "GET VAR ups battery.charge" ["LIST CMD ups" ...]
  nutclient.py HOST login USER PASS # USERNAME/PASSWORD/LOGIN/LOGOUT handshake

Lines are sent verbatim with a trailing LF; replies are printed as received.
"""
import socket, sys, time

def connect(host, port=3493):
    s = socket.create_connection((host, port), timeout=5)
    return s, s.makefile("r", encoding="utf-8", newline="\n")

def cmd(sock, rd, line):
    sock.sendall((line + "\n").encode())
    out = []
    first = rd.readline()
    if not first:
        return out
    out.append(first.rstrip("\n"))
    if first.startswith("BEGIN LIST"):
        while True:
            l = rd.readline()
            if not l:
                break
            out.append(l.rstrip("\n"))
            if l.startswith("END LIST"):
                break
    return out

def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    host = sys.argv[1]
    mode = sys.argv[2] if len(sys.argv) > 2 else "upsc"
    sock, rd = connect(host)
    if mode == "upsc":
        ups = cmd(sock, rd, "LIST UPS")
        print("\n".join(ups))
        name = ups[1].split()[1] if len(ups) > 1 else "ups"
        for l in cmd(sock, rd, f"LIST VAR {name}"):
            if l.startswith("VAR "):
                _, _, var, val = l.split(" ", 3)
                print(f"{var}: {val.strip(chr(34))}")
        print("\n".join(cmd(sock, rd, f"LIST CMD {name}")))
    elif mode == "status":
        name = sys.argv[3] if len(sys.argv) > 3 else "ups"
        while True:
            print(time.strftime("%H:%M:%S"), " ".join(cmd(sock, rd, f"GET VAR {name} ups.status")),
                  " ".join(cmd(sock, rd, f"GET VAR {name} battery.charge")), flush=True)
            time.sleep(1)
    elif mode == "raw":
        for line in sys.argv[3:]:
            print(f"> {line}")
            print("\n".join(cmd(sock, rd, line)))
    elif mode == "login":
        user, pw = sys.argv[3], sys.argv[4]
        for line in (f"USERNAME {user}", f"PASSWORD {pw}", "LOGIN ups", "GET NUMLOGINS ups", "LIST CLIENT ups", "LOGOUT"):
            print(f"> {line}"); print("\n".join(cmd(sock, rd, line)))
    sock.close()

if __name__ == "__main__":
    main()
