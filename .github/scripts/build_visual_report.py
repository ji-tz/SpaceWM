#!/usr/bin/env python3
"""把 CI 产物整理成一份 PR 评论用的 markdown 报告。

输入：report/ 目录（*.png 截图 + junit.xml）
输出：report/report.md（图片先用相对路径，评论脚本会重写成 raw.githubusercontent URL）

用法：
    python3 build_visual_report.py --dir report --out report/report.md --run-id 123 --sha abcdef
"""
from __future__ import annotations

import argparse
import pathlib
import xml.etree.ElementTree as ET


def parse_junit(path: pathlib.Path) -> dict:
    """返回 {'tests': n, 'failures': n, 'errors': n, 'skipped': n, 'time': s, 'cases': [(name, status, secs)]}"""
    out = {"tests": 0, "failures": 0, "errors": 0, "skipped": 0, "time": 0.0, "cases": []}
    if not path.exists():
        return out
    root = ET.parse(path).getroot()
    for suite in root.iter("testsuite"):
        out["tests"] += int(suite.get("tests", 0))
        out["failures"] += int(suite.get("failures", 0))
        out["errors"] += int(suite.get("errors", 0))
        out["skipped"] += int(suite.get("skipped", 0))
        out["time"] += float(suite.get("time", 0) or 0)
    for case in root.iter("testcase"):
        status = "✅"
        for child in case:
            if child.tag == "failure":
                status = "❌"
            elif child.tag == "skipped":
                status = "⏭️"
        out["cases"].append((case.get("classname") or case.get("name", "?"), status, case.get("time", "")))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="report")
    ap.add_argument("--out", default="report/report.md")
    ap.add_argument("--run-id", default="")
    ap.add_argument("--sha", default="")
    ap.add_argument("--title", default="SpaceWM CI")
    args = ap.parse_args()

    d = pathlib.Path(args.dir)
    out = pathlib.Path(args.out)
    shots = sorted(p for p in d.glob("*.png"))
    j = parse_junit(d / "junit.xml")

    lines: list[str] = []
    test_ok = j["failures"] == 0 and j["errors"] == 0
    lines.append(f"## {'✅' if test_ok else '❌'} {args.title}")
    lines.append("")
    lines.append("| 检查 | 结果 |")
    lines.append("|---|---|")
    if j["tests"]:
        skipped_note = f"，跳过 {j['skipped']}" if j["skipped"] else ""
        passed = j["tests"] - j["failures"] - j["errors"]
        lines.append(f"| ctest | {'✅' if test_ok else '❌'} {passed}/{j['tests']} 通过（{j['time']:.1f}s{skipped_note}） |")
    else:
        lines.append("| ctest | ⚠️ 未取到 junit.xml |")
    lines.append(f"| 截图 | {len(shots)} 张 |")
    if args.sha:
        lines.append(f"| commit | `{args.sha[:10]}` |")
    lines.append("")

    if j["cases"]:
        failed = [c for c in j["cases"] if c[1] == "❌"]
        if failed:
            lines.append("**失败用例**")
            lines.append("")
            for name, _, _ in failed[:15]:
                lines.append(f"- ❌ `{name}`")
            lines.append("")

    if shots:
        lines.append("<details open><summary>截图</summary>")
        lines.append("")
        for p in shots:
            lines.append(f"**{p.stem}**")
            lines.append("")
            lines.append(f"![{p.stem}]({p.name})")
            lines.append("")
        lines.append("</details>")
    else:
        lines.append("_本次运行没有产生截图（`artifacts/screenshots/` 为空）。_")

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(out.read_text(encoding="utf-8")[:2000])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
