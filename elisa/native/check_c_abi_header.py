#!/usr/bin/env python3
import re
import sys
from pathlib import Path


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    source = re.sub(r"//.*", "", source)
    return source


def normalize_type(value: str) -> str:
    value = " ".join(value.replace("\n", " ").split())
    value = value.replace(" *", "*").replace("* ", "*")
    value = re.sub(r"\s+", " ", value)
    return value.strip()


def field_signature(field: str) -> tuple[str, str]:
    field = normalize_type(field.strip().rstrip(";"))
    match = re.match(r"(.+?)([A-Za-z_][A-Za-z0-9_]*)$", field)
    if not match:
        raise ValueError(f"cannot parse field declaration: {field!r}")
    return normalize_type(match.group(1)), match.group(2)


def param_type(param: str) -> str:
    param = normalize_type(param.strip())
    if param == "void" or not param:
        return param
    match = re.match(r"(.+?)([A-Za-z_][A-Za-z0-9_]*)$", param)
    if match:
        return normalize_type(match.group(1))
    return param


def parse_structs(source: str) -> dict[str, list[tuple[str, str]]]:
    structs: dict[str, list[tuple[str, str]]] = {}
    pattern = re.compile(r"(?:typedef\s+)?struct\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:[A-Za-z_][A-Za-z0-9_]*)?\s*\{(.*?)\}\s*(?:[A-Za-z_][A-Za-z0-9_]*)?\s*;", re.S)
    for match in pattern.finditer(source):
        name = match.group(1)
        body = match.group(2)
        fields = [field_signature(part) for part in body.split(";") if part.strip()]
        structs[name] = fields
    return structs


def parse_functions(source: str) -> dict[str, tuple[str, list[str]]]:
    functions: dict[str, tuple[str, list[str]]] = {}
    pattern = re.compile(r"(^|\n)\s*([A-Za-z_][A-Za-z0-9_]*(?:\s*\*)?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\((.*?)\)\s*;", re.S)
    for match in pattern.finditer(source):
        return_type = normalize_type(match.group(2))
        name = match.group(3)
        if name in {"typedef"}:
            continue
        raw_params = match.group(4)
        params = [param_type(part) for part in raw_params.split(",") if part.strip()]
        functions[name] = (return_type, params)
    return functions


def parse_header(path: Path) -> tuple[dict[str, list[tuple[str, str]]], dict[str, tuple[str, list[str]]]]:
    source = strip_comments(path.read_text())
    return parse_structs(source), parse_functions(source)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_c_abi_header.py <checked-in-header> <generated-header>", file=sys.stderr)
        return 2

    checked_path = Path(sys.argv[1])
    generated_path = Path(sys.argv[2])
    checked_structs, checked_functions = parse_header(checked_path)
    generated_structs, generated_functions = parse_header(generated_path)

    errors: list[str] = []
    for name, generated_fields in generated_structs.items():
        checked_fields = checked_structs.get(name)
        if checked_fields is None:
            errors.append(f"missing struct {name} in {checked_path}")
        elif checked_fields != generated_fields:
            errors.append(f"struct {name} drift:\n  checked:  {checked_fields}\n  generated:{generated_fields}")

    for name, generated_signature in generated_functions.items():
        checked_signature = checked_functions.get(name)
        if checked_signature is None:
            errors.append(f"missing function {name} in {checked_path}")
        elif checked_signature != generated_signature:
            errors.append(f"function {name} drift:\n  checked:  {checked_signature}\n  generated:{generated_signature}")

    if errors:
        print("Elisa C ABI header drift detected:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print(f"Elisa C ABI header check ok: {checked_path.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
