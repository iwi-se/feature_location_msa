#!/usr/bin/env python3
"""
Generate a msa_analyzer --system-id-map file from an SPL feature spec and a
directory of per-variant feature-selection files.

The SPL spec file must contain a block like:

    FEATURES 8

    COGNITIVE
    LOGGING
    ACTIVITYDIAGRAM
    STATEDIAGRAM
    SEQUENCEDIAGRAM
    USECASEDIAGRAM
    COLLABORATIONDIAGRAM
    DEPLOYEMENTDIAGRAM

The features directory must contain one file per variant, named exactly like
the variant's source directory, each listing the features included in that
variant (one per line), e.g.:

    LOGGING
    STATEDIAGRAM
    SEQUENCEDIAGRAM

The system id for a variant is the binary number where bit i (0 = least
significant, i.e. the rightmost digit) is 1 iff the i-th feature listed in
the SPL spec (0-indexed, top to bottom) is present in that variant. E.g. for
the spec above, a variant containing only COGNITIVE is id 1 (00000001), and
a variant containing only DEPLOYEMENTDIAGRAM is id 128 (10000000).
"""

import argparse
import sys
from pathlib import Path
from typing import List


def parse_features(spl_spec_path: Path) -> List[str]:
    """
    Parse the ordered list of feature names out of the FEATURES block of an
    SPL spec file. Feature index in the returned list corresponds to its bit
    position (index 0 = least significant bit).
    """
    lines = spl_spec_path.read_text(encoding="utf-8").splitlines()

    count = None
    start = None
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("FEATURES"):
            parts = stripped.split()
            if len(parts) < 2 or not parts[1].isdigit():
                print(
                    f"Error: malformed FEATURES line in {spl_spec_path}: "
                    f"{line!r}",
                    file=sys.stderr,
                )
                sys.exit(1)
            count = int(parts[1])
            start = i + 1
            break

    if count is None:
        print(f"Error: no FEATURES line found in {spl_spec_path}", file=sys.stderr)
        sys.exit(1)

    features = []
    for line in lines[start:]:
        stripped = line.strip()
        if not stripped:
            continue
        features.append(stripped)
        if len(features) == count:
            break

    if len(features) != count:
        print(
            f"Error: FEATURES declared {count} features but only found "
            f"{len(features)} in {spl_spec_path}",
            file=sys.stderr,
        )
        sys.exit(1)

    return features


def read_variant_features(variant_file: Path) -> List[str]:
    return [
        line.strip()
        for line in variant_file.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]


def compute_system_id(features: List[str], variant_features: List[str]) -> int:
    feature_index = {feature: i for i, feature in enumerate(features)}

    system_id = 0
    for feature in variant_features:
        if feature not in feature_index:
            print(
                f"Error: unknown feature {feature!r} not present in SPL spec",
                file=sys.stderr,
            )
            sys.exit(1)
        system_id |= 1 << feature_index[feature]

    return system_id


def generate_mapping(spl_spec_path: Path, features_dir: Path) -> List[tuple]:
    features = parse_features(spl_spec_path)

    mapping = []
    for variant_file in sorted(features_dir.iterdir()):
        if not variant_file.is_file():
            continue
        variant_name = variant_file.name
        variant_features = read_variant_features(variant_file)
        system_id = compute_system_id(features, variant_features)
        mapping.append((variant_name, system_id))

    return mapping


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Generate a msa_analyzer --system-id-map file from an SPL "
            "feature spec and a directory of per-variant feature-selection "
            "files."
        )
    )
    parser.add_argument("spl_spec", type=Path, help="Path to the SPL spec file")
    parser.add_argument(
        "features_dir",
        type=Path,
        help="Directory containing one feature-selection file per variant",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="Output mapping file path (default: stdout)",
    )
    args = parser.parse_args()

    if not args.spl_spec.is_file():
        print(f"Error: {args.spl_spec} is not a file", file=sys.stderr)
        sys.exit(1)
    if not args.features_dir.is_dir():
        print(f"Error: {args.features_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    mapping = generate_mapping(args.spl_spec, args.features_dir)

    lines = [f"{variant_name},{system_id}" for variant_name, system_id in mapping]
    output_text = "\n".join(lines) + "\n"

    if args.output is not None:
        args.output.write_text(output_text, encoding="utf-8")
    else:
        sys.stdout.write(output_text)


if __name__ == "__main__":
    main()
