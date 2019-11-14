#!/usr/bin/env python3

import io
import subprocess
from functools import partial
from http.server import SimpleHTTPRequestHandler, test, HTTPStatus


"""
Usage:
    python3 ibndbHttp.py [--bind <address>] [<port>]
"""


class RequestHandler(SimpleHTTPRequestHandler):

    def do_GET(self):
        command = "/root/dkipnis/ibnbd-tool/ibnbd dump json all"
        proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=None,
                                shell=True)
        jsonstr = proc.communicate()[0]
        f = io.BytesIO()

        ret = proc.wait()
        if ret != 0 or not jsonstr:
            f.write(b'')
        else:
            f.write(jsonstr)
        f.seek(0)

        self.send_response(HTTPStatus.OK)
        self.send_header("Content-type", "application/json; charset=utf-8")
        self.send_header("Content-Length", len(jsonstr.decode('utf-8')))
        self.end_headers()
        try:
            self.copyfile(f, self.wfile)
        finally:
            f.close()


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument('--bind', '-b', default='', metavar='ADDRESS',
                        help='Specify alternate bind address '
                             '[default: all interfaces]')
    parser.add_argument('port', action='store',
                        default=8000, type=int,
                        nargs='?',
                        help='Specify alternate port [default: 8000]')
    args = parser.parse_args()
    handler_class = partial(RequestHandler)
    test(HandlerClass=handler_class, port=args.port, bind=args.bind)
