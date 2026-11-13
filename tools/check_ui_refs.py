#!/usr/bin/env python3
"""Check that static/app.js only reaches DOM elements that static/index.html defines.

This exists because a deleted button took the whole page down: the button and its
entry in the `elements` map were removed, but one `addEventListener` line was left
behind, so app.js threw on load and never polled /api/status. The page looked like
the controller was offline and switched off, while the API was reporting online and
DHW-only. `node --check` passes: the file was syntactically perfect.

Usage: tools/check_ui_refs.py [--html static/index.html] [--js static/app.js]
Exit 0 when every reference resolves, 1 otherwise.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def html_ids(path):
    with open(path, encoding="utf-8") as handle:
        return set(re.findall(r'id="([^"]+)"', handle.read()))


def element_map(js):
    """`foo: document.getElementById('bar')` -> {foo: bar}. Concatenated ids are skipped."""
    pattern = re.compile(r"(\w+):\s*document\.getElementById\('([^']+)'\)")
    return {name: element_id for name, element_id in pattern.findall(js)}


def dereferenced(js):
    """Every `elements.foo.<member>` the script touches."""
    pattern = re.compile(r"elements\.(\w+)\.(?:addEventListener|textContent|value|classList|innerHTML|style|checked|min|max|disabled|scrollTop|focus|blur)")
    return sorted({match for match in pattern.findall(js)})


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--html", default=os.path.join(HERE, "static", "index.html"))
    parser.add_argument("--js", default=os.path.join(HERE, "static", "app.js"))
    args = parser.parse_args()

    ids = html_ids(args.html)
    mapping = element_map(open(args.js, encoding="utf-8").read())
    used = dereferenced(open(args.js, encoding="utf-8").read())

    problems = []
    for name, element_id in sorted(mapping.items()):
        if element_id not in ids:
            problems.append("app.js maps elements.%s to id '%s', which index.html does not define"
                            % (name, element_id))
    for name in used:
        if name not in mapping:
            problems.append("app.js dereferences elements.%s, which is not in the elements map" % name)

    if problems:
        print("FAIL: %d broken UI reference(s) in %s" % (len(problems), args.js))
        for problem in problems:
            print("  - %s" % problem)
        print("\nA broken reference throws on load and the page then never polls /api/status,")
        print("which shows up as a permanently offline controller.")
        return 1

    unused = sorted(set(mapping) - set(used))
    print("OK: %d element(s) mapped, all %d dereferenced ones defined by %s"
          % (len(mapping), len(used), args.html))
    if unused:
        print("     not read by the script (harmless, possibly dead): %s" % ", ".join(unused))
    return 0


if __name__ == "__main__":
    sys.exit(main())
