// WP-004 front-end smoke: a structural netlist that exercises port binding,
// a continuous assignment, and a gate-level boolean expression. The pinned
// Yosys must elaborate it and write JSON without synthesis or ABC.
module smoke (
    input a,
    input b,
    output y
);
    wire n;
    assign n = a & b;
    assign y = n | ~b;
endmodule
