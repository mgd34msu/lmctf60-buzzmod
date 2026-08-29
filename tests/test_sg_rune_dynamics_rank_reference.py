#!/usr/bin/env python3
"""Exact portable oracle for the binary32 simplex-rank corpus."""

from fractions import Fraction
import math
from pathlib import Path
import re
import struct


DIMENSIONS = 7
PRIME_COUNT = 68


def binary32(value: float) -> Fraction:
    stored = struct.unpack("!f", struct.pack("!f", value))[0]
    return Fraction.from_float(stored)


def determinant(matrix: list[list[Fraction]]) -> Fraction:
    result = Fraction(1)
    for column in range(DIMENSIONS):
        pivot = next(
            (row for row in range(column, DIMENSIONS)
             if matrix[row][column]),
            None,
        )
        if pivot is None:
            return Fraction(0)
        if pivot != column:
            matrix[column], matrix[pivot] = matrix[pivot], matrix[column]
            result = -result
        pivot_value = matrix[column][column]
        result *= pivot_value
        for row in range(column + 1, DIMENSIONS):
            factor = matrix[row][column] / pivot_value
            for trailing in range(column + 1, DIMENSIONS):
                matrix[row][trailing] -= factor * matrix[column][trailing]
    return result


def validate_moduli() -> None:
    source = (Path(__file__).parents[1] /
              "slipgate/sg_rune_dynamics_geometry.c").read_text()
    moduli = [int(value) for value in re.findall(
        r"UINT32_C\((100[0-9]+)\)", source
    )]
    if len(moduli) != PRIME_COUNT or len(set(moduli)) != PRIME_COUNT:
        raise SystemExit("binary32 rank moduli are not distinct")
    for modulus in moduli:
        if modulus <= 2**29 or any(
                modulus % divisor == 0
                for divisor in range(2, math.isqrt(modulus) + 1)):
            raise SystemExit(f"binary32 rank modulus {modulus} is not prime")


def main() -> None:
    validate_moduli()
    final_component = binary32(1e-16)
    matrix = [
        [binary32(1.0 if row == column else 0.0)
         for column in range(DIMENSIONS)]
        for row in range(DIMENSIONS)
    ]
    matrix[-1][-1] = final_component
    exact = determinant(matrix)
    if exact != final_component or exact == 0:
        raise SystemExit("near-degenerate binary32 simplex lost exact rank")
    print("binary32 simplex rank reference: full rank")


if __name__ == "__main__":
    main()
