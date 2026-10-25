#!/usr/bin/env python3
"""Turns EasyList and EasyPrivacy (https://easylist.to) into build/blocklist.txt,
their blocked domains one per line; the app fails the engine's requests to them
(src/browser.cpp, Fetch.requestPaused). Only rules that block a domain carry
over (||domain^, alone or limited to third-party or request types): the app
checks the host alone.
Run from anywhere; tools/build.sh runs it when the file is missing."""
import os
import re
import urllib.request

LISTS = ["https://easylist.to/easylist/easylist.txt",
         "https://easylist.to/easylist/easyprivacy.txt"]
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "blocklist.txt")

BLOCK = re.compile(r"^\|\|([a-z0-9.-]+)\^(?:\$([a-z0-9,-]+))?$")
# Options that still mean "this domain's requests": third-party, and limits
# to request types (a domain blocked for scripts or XHR is a tracker to block
# whole). Anything else (domain=, popup, ~types, ...) keeps the rule out.
WHOLE_DOMAIN = {"third-party", "3p", "script", "xmlhttprequest", "image", "ping",
                "subdocument", "media", "object", "websocket", "font", "stylesheet",
                "other", "important"}
# Allow rules that hold everywhere; ones tied to sites ($domain=...) are left out.
ALLOW = re.compile(r"^@@\|\|([a-z0-9.-]+)\^(?:\$(?:third-party|document))?$")


def main():
    blocked, allowed, versions = set(), set(), []
    for url in LISTS:
        # easylist.to turns away Python's default User-Agent.
        request = urllib.request.Request(url, headers={"User-Agent": "chromeberry fetch-blocklist"})
        text = urllib.request.urlopen(request, timeout=60).read().decode("utf-8")
        version = re.search(r"^! Version: (\S+)", text, re.M)
        versions.append("%s %s" % (url.rsplit("/", 1)[1], version.group(1) if version else "?"))
        for line in text.splitlines():
            line = line.strip().lower()
            m = BLOCK.match(line)
            if m:
                if not m.group(2) or set(m.group(2).split(",")) <= WHOLE_DOMAIN:
                    blocked.add(m.group(1).strip("."))
                continue
            m = ALLOW.match(line)
            if m:
                allowed.add(m.group(1).strip("."))
    blocked -= allowed

    # A domain also blocks its subdomains, so those need no entry of their own.
    def covered(domain):
        parts = domain.split(".")
        return any(".".join(parts[i:]) in blocked for i in range(1, len(parts) - 1))
    domains = sorted(d for d in blocked if "." in d and not covered(d))

    # A comment line matches no host.
    text = ("! ChromeBerry's content blocklist: the whole-domain rules of EasyList and\n"
            "! EasyPrivacy (https://easylist.to: %s), dual licensed GPLv3 / CC BY-SA 3.0\n"
            "! by The EasyList authors. Each domain also blocks its subdomains.\n"
            % ", ".join(versions)) + "\n".join(domains) + "\n"
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print("%s: %d domains (%s)" % (os.path.normpath(OUT), len(domains), ", ".join(versions)))


if __name__ == "__main__":
    main()
