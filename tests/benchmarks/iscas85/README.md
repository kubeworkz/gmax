# ISCAS'85 benchmarks: c432 and c499

These are the original gate-level Verilog netlists from the ISCAS'85 benchmark
suite, used by WP-011 to exercise the pinned Yosys frontend, fault collapse, and
PODEM end to end.

## Source

- Directory: <https://www.pld.ttu.ee/~maksim/benchmarks/iscas85/verilog/>
- Files: `c432.v`, `c499.v`
- Retrieved: 2026-11-24
- Original mirror timestamps: 2007-02-01

The files were converted from CRLF to LF line endings so they are stable across
checkouts; no gate, port, or netlist content was changed. The source hashes
below are for the checked-in LF files.

| file | SHA-256 |
| --- | --- |
| `c432.v` | `129b1d08d6f3a37d550db82627a34f9e34d83b3358347ebaa2962bbe23cf5efb` |
| `c499.v` | `64de0ea40b5e439b34843ce71352011f881f228ec4502736bb15eb3e674ec478` |

## Expected ingest

Yosys 0.71 lowers the multi-input gates to the scalar primitives in the
flattened database:

| design | Yosys gates/nets | netlist hash |
| --- | --- | --- |
| c432 | 314 gates, 350 nets | `413dd789` |
| c499 | 246 gates, 287 nets | `bebace97` |

## Expected ATPG result

PODEM runs first. A budget-limited abort is handed to the two-copy SAT miter:
SAT tests are replayed through the scalar fault simulator, and an UNSAT miter
is a redundancy proof. This is why the five c432 outcomes below are reported as
untestable rather than simply as aborted:

| design | detected | untestable | aborted | raw coverage | testable coverage |
| --- | --- | --- | --- | --- | --- |
| c432 | 479 | 5 | 0 | `692/700` | `479/479` |
| c499 | 574 | 0 | 0 | `574/574` | `574/574` |

The five proven redundant c432 faults are net 51 SA0, 118 SA0, 165 SA1, 166 SA1
and 167 SA1. The SAT models recover the other aborted faults, including 168 SA1
and 171–178 SA0 on c432 and 270 SA1/282 SA1 on c499. The report records
`method: "sat"` on those outcomes; every other outcome is `method: "podem"`.

The end-to-end process test runs:

```text
gmax read_verilog tests/benchmarks/iscas85/c432.v c432.dfdb c432
gmax write_faults c432.dfdb c432.faults
gmax run_atpg c432.dfdb c432.faults 100000 0 c432-report
```

and the same sequence for c499. It is skipped when `GMAX_YOSYS` does not name
the pinned Yosys binary.

## License

The mirror does not ship a separate license file. The ISCAS'85 netlists are the
public benchmark suite published by Brglez, Fujiwara, and Malik and are included
here as benchmark input data; retain this provenance when redistributing them.
