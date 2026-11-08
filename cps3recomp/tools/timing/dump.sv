// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp). Run against jtcores' SH7604 model (GPL-3.0-or-later),
// which is not in this repository.
// sf3comp's timing harness: the SH7604 model's decoder (SH_pkg.sv's Decode) for every opcode and state, as a table
module dump;
    import SH2_PKG::*;
    integer f, op, st;
    DecInstr_t d;
    initial begin
        f = $fopen("decode.txt", "w");
        for (op = 0; op < 65536; op = op + 1) begin
            for (st = 0; st < 8; st = st + 1) begin
                d = Decode(op[15:0], st[2:0], 1'b0, 1'b1);
                $fwrite(f, "%04x %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d\n",
                        op, st, d.RA.N, d.RA.R, d.RA.W, d.RB.N, d.RB.R, d.RB.W, d.R0R, d.MEM.R, d.MEM.W,
                        d.MAC.R, d.MAC.W, d.LST, d.BR.BI, d.BR.BT, d.IMMT, d.ILI, d.BR.BD);
                if (st[2:0] == d.LST) st = 8;
            end
        end
        $fclose(f);
        $finish;
    end
endmodule
