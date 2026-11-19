#!/usr/bin/env python3
"""Writes tomline/browser/index.html, a standalone copy of the browser model
(tomline/browser/tomline.html is the page body used for the shared online
version). Run from the eurorack root after editing tomline.html:

  python3 tomline/tools/make_standalone.py
"""
src = open('tomline/browser/tomline.html', encoding='utf-8').read()
page = ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
        '</head>\n<body style="margin:0">\n' + src + '\n</body>\n</html>\n')
open('tomline/browser/index.html', 'w', encoding='utf-8').write(page)
print('wrote tomline/browser/index.html')
