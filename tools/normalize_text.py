# -*- coding: utf-8 -*-
"""把文本文件统一成 Windows 习惯：UTF-8 BOM + CRLF。

说明.txt 和安装脚本都是这个格式（PowerShell 5.1 读中文必须有 BOM，
cmd 读 .bat 必须 CRLF）。改完文档后跑一次即可：

    python tools/normalize_text.py 说明.txt
"""

import sys


def normalize(path):
    with open(path, "rb") as f:
        data = f.read()
    text = data.decode("utf-8-sig")
    text = text.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n")
    with open(path, "wb") as f:
        f.write(b"\xef\xbb\xbf")
        f.write(text.encode("utf-8"))
    print("normalized (UTF-8 BOM + CRLF): %s (%d bytes)" % (path, len(text.encode("utf-8")) + 3))


def main():
    if not sys.argv[1:]:
        print(__doc__)
        return 1
    for path in sys.argv[1:]:
        normalize(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
