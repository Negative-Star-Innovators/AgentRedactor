#!/usr/bin/env python3
"""Generates the bench input: realistic business-prose paragraphs seeded with
PII covering all 8 supported types (person, email, phone, address, url,
account_number, date, secret). Deterministic output reused across runs.
Writes input.txt next to this script and prints workload stats."""
import os
import random

random.seed(20261008)
HERE = os.path.dirname(os.path.abspath(__file__))

FIRST = ["Margaret", "Jonathan", "Priya", "Diego", "Aisha", "Tom", "Elena", "Marcus",
         "Hannah", "Kenji", "Rachel", "Omar", "Ingrid", "Felix", "Nadia", "Peter",
         "Sofia", "Andre", "Lucy", "Viktor", "Grace", "Hassan", "Julia", "Nathan"]
LAST = ["Calloway", "Reyes", "Okafor", "Lindqvist", "Moreau", "Tanaka", "Whitfield",
        "Novak", "Herrera", "Kim", "Ashford", "Petrov", "Nguyen", "Barlow", "Silva",
        "Grunwald", "Mercer", "Odell", "Fontaine", "Castillo", "Brennan", "Iqbal"]
STREET = ["Maple", "Cedar", "Harbor", "Willow", "Beacon", "Juniper", "Ridge",
          "Colfax", "Marina", "Sycamore", "Division", "Lexington", "Alder"]
CITY = ["Portland, OR 97209", "Austin, TX 78701", "Denver, CO 80202",
        "Chicago, IL 60607", "Seattle, WA 98101", "Boston, MA 02110"]
DOMAIN = ["northwind-trading.com", "bluepeak.io", "summitledger.co", "harborlabs.net",
          "ferrostat.com", "alderandmain.org"]
TLD_PATH = ["/portal/login", "/reports/q3-2026", "/api/v2/sync", "/account/billing",
            "/docs/handbook", "/dashboard/overview"]

FILLER_OPEN = [
    "Following up on our call this morning,",
    "As discussed in the weekly sync,",
    "Per the action items from yesterday's review,",
    "Quick note before the all-hands on Thursday —",
    "The vendor onboarding checklist is nearly done;",
    "Before we sign off on the migration window,",
    "I reviewed the support escalations overnight and,",
    "Drafting the client summary now;",
    "Accounting flagged a discrepancy, so",
    "For the compliance audit trail,",
]
FILLER_MID = [
    "the remaining paperwork should reach your desk by Friday.",
    "we still need sign-off from finance before anything ships.",
    "the timeline holds as long as staging stays green.",
    "no blockers on our side, but legal wants another pass.",
    "I've attached the revised scope for a second opinion.",
    "the rollback plan is documented in the runbook.",
    "let me know if the numbers look off to you.",
    "the team agreed to defer the deprecation by one sprint.",
]
SECRET_KINDS = [
    lambda: "sk-live-" + "".join(random.choices("abcdef0123456789", k=32)),
    lambda: "AKIA" + "".join(random.choices("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", k=16)),
    lambda: "ghp_" + "".join(random.choices("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", k=36)),
    lambda: "".join(random.choices("0123456789", k=6)) + "-" + "".join(random.choices("0123456789", k=7)),
]


def person():
    return random.choice(FIRST) + " " + random.choice(LAST)


def email():
    f = random.choice(FIRST).lower()
    l = random.choice(LAST).lower()
    sep = random.choice([".", "_", ""])
    return f"{f}{sep}{l}@{random.choice(DOMAIN)}"


def phone():
    a, b, c = (random.randint(200, 989), random.randint(200, 989), random.randint(1000, 9999))
    return random.choice([f"({a}) {b}-{c}", f"+1-{a}-{b}-{c}", f"{a}.{b}.{c}"])


def address():
    return f"{random.randint(12, 9899)} {random.choice(STREET)} {random.choice(['St', 'Ave', 'Blvd', 'Rd'])}, {random.choice(CITY)}"


def url():
    return f"https://{random.choice(DOMAIN)}{random.choice(TLD_PATH)}"


def account():
    return random.choice([
        lambda: f"IBAN DE{random.randint(10,99)} {random.randint(1000,9999)} {random.randint(1000,9999)} {random.randint(1000,9999)}",
        lambda: f"routing {random.randint(100000000, 999999999)}, account {random.randint(1000000000, 9999999999)}",
        lambda: f"acct #{random.randint(10000000, 99999999)}",
    ])()


def date():
    m, d, y = random.randint(1, 12), random.randint(1, 28), random.choice([2025, 2026, 2027])
    return random.choice([f"{m:02d}/{d:02d}/{y}", f"{y}-{m:02d}-{d:02d}", f"{random.choice(['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'])} {d}, {y}"])


PII = [person, email, phone, address, url, account, date, lambda: random.choice(SECRET_KINDS)()]


def paragraph(i):
    open_ = random.choice(FILLER_OPEN)
    mid = random.choice(FILLER_MID)
    style = i % 4
    if style == 0:  # two PII items woven into a sentence
        a, b = random.sample(PII, 2)
        return (f"{open_} {a()} mentioned that {b()} is on file, and {mid} ")
    if style == 1:  # contact block
        return (f"Contact: {person()}, {email()}, {phone()}. Office: {address()}. "
                f"Profile: {url()}. Next check-in: {date()}. ")
    if style == 2:  # billing / credentials
        return (f"Billing: {account()} (verify against {url()}). "
                f"API key on record: {random.choice(SECRET_KINDS)()}. Owner: {person()}. ")
    return f"{open_} {mid} "  # plain prose, no PII


def main():
    parts, total = [], 0
    i = 0
    while total < 150 * 1024:
        p = paragraph(i)
        parts.append(p)
        total += len(p.encode("utf-8"))
        i += 1
    text = "\n\n".join(parts) + "\n"
    out = os.path.join(HERE, "input.txt")
    with open(out, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"wrote {out}: {len(text.encode('utf-8'))} bytes, {len(parts)} paragraphs, {i} blocks")


if __name__ == "__main__":
    main()
