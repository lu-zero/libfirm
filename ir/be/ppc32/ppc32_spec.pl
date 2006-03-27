# Creation: 2006/02/13
# $Id$
# This is a template specification for the Firm-Backend

# the cpu architecture (ia32, ia64, mips, sparc, ppc32, ...)

$arch = "ppc32";

$comment_string = "/*";

# The node description is done as a perl hash initializer with the
# following structure:
#
# %nodes = (
#
# <op-name> => {
#   "op_flags"  => "N|L|C|X|I|F|Y|H|c|K",
#   "irn_flags" => "R|N|I"
#   "arity"     => "0|1|2|3 ... |variable|dynamic|any",
#   "state"     => "floats|pinned|mem_pinned|exc_pinned",
#   "args"      => [
#                    { "type" => "type 1", "name" => "name 1" },
#                    { "type" => "type 2", "name" => "name 2" },
#                    ...
#                  ],
#   "comment"   => "any comment for constructor",
#   "reg_req"   => { "in" => [ "reg_class|register" ], "out" => [ "reg_class|register|in_rX" ] },
#   "cmp_attr"  => "c source code for comparing node attributes",
#   "emit"      => "emit code with templates",
#   "rd_constructor" => "c source code which constructs an ir_node"
# },
#
# ... # (all nodes you need to describe)
#
# ); # close the %nodes initializer

# op_flags: flags for the operation, OPTIONAL (default is "N")
# the op_flags correspond to the firm irop_flags:
#   N   irop_flag_none
#   L   irop_flag_labeled
#   C   irop_flag_commutative
#   X   irop_flag_cfopcode
#   I   irop_flag_ip_cfopcode
#   F   irop_flag_fragile
#   Y   irop_flag_forking
#   H   irop_flag_highlevel
#   c   irop_flag_constlike
#   K   irop_flag_keep
#
# irn_flags: special node flags, OPTIONAL (default is 0)
# following irn_flags are supported:
#   R   rematerializeable
#   N   not spillable
#   I   ignore for register allocation
#
# state: state of the operation, OPTIONAL (default is "floats")
#
# arity: arity of the operation, MUST NOT BE OMITTED
#
# args:  the OPTIONAL arguments of the node constructor (debug, irg and block
#        are always the first 3 arguments and are always autmatically
#        created)
#        If this key is missing the following arguments will be created:
#        for i = 1 .. arity: ir_node *op_i
#        ir_mode *mode
#
# comment: OPTIONAL comment for the node constructor
#
# rd_constructor: for every operation there will be a
#      new_rd_<arch>_<op-name> function with the arguments from above
#      which creates the ir_node corresponding to the defined operation
#      you can either put the complete source code of this function here
#
#      This key is OPTIONAL. If omitted, the following constructor will
#      be created:
#      if (!op_<arch>_<op-name>) assert(0);
#      for i = 1 to arity
#         set in[i] = op_i
#      done
#      res = new_ir_node(db, irg, block, op_<arch>_<op-name>, mode, arity, in)
#      return res
#
# NOTE: rd_constructor and args are only optional if and only if arity is 0,1,2 or 3

# register types:
#   0 - no special type
#   1 - caller save (register must be saved by the caller of a function)
#   2 - callee save (register must be saved by the called function)
#   4 - ignore (do not assign this register)
# NOTE: Last entry of each class is the largest Firm-Mode a register can hold
%reg_classes = (
  "general_purpose" => [
                         { "name" => "r0", "type" => 1 },
                         { "name" => "r2", "type" => 1 },
                         { "name" => "r3", "type" => 1 },
                         { "name" => "r4", "type" => 1 },
                         { "name" => "r5", "type" => 1 },
                         { "name" => "r6", "type" => 1 },
                         { "name" => "r7", "type" => 1 },
                         { "name" => "r8", "type" => 1 },
                         { "name" => "r9", "type" => 1 },
                         { "name" => "r10", "type" => 1 },
#                        { "name" => "r11", "type" => 1 },
#                        { "name" => "r12", "type" => 1 },
                         { "name" => "r13", "type" => 2 },
                         { "name" => "r14", "type" => 2 },
                         { "name" => "r15", "type" => 2 },
#                        { "name" => "r16", "type" => 2 },
#                        { "name" => "r17", "type" => 2 },
#                        { "name" => "r18", "type" => 2 },
#                        { "name" => "r19", "type" => 2 },
#                        { "name" => "r20", "type" => 2 },
#                        { "name" => "r21", "type" => 2 },
#                        { "name" => "r22", "type" => 2 },
#                        { "name" => "r23", "type" => 2 },
#                        { "name" => "r24", "type" => 2 },
#                        { "name" => "r25", "type" => 2 },
#                        { "name" => "r26", "type" => 2 },
#                        { "name" => "r27", "type" => 2 },
#                        { "name" => "r28", "type" => 2 },
#                        { "name" => "r29", "type" => 2 },
#                        { "name" => "r30", "type" => 2 },
                         { "name" => "r31", "type" => 2 },
                         { "name" => "r1", "type" => 6 }, # this is our stackpointer
                         { "mode" => "mode_P" }
                       ],
  "floating_point"  => [
#                        { "name" => "f0", "type" => 1 }, # => reserved for FP Perm
                         { "name" => "f1", "type" => 1 },
                         { "name" => "f2", "type" => 1 },
                         { "name" => "f3", "type" => 1 },
                         { "name" => "f4", "type" => 1 },
                         { "name" => "f5", "type" => 1 },
                         { "name" => "f6", "type" => 1 },
                         { "name" => "f7", "type" => 1 },
                         { "name" => "f8", "type" => 1 },
                         { "name" => "f9", "type" => 1 },
                         { "name" => "f10", "type" => 1 },
                         { "name" => "f11", "type" => 1 },
                         { "name" => "f12", "type" => 1 },
                         { "name" => "f13", "type" => 1 },
                         { "name" => "f14", "type" => 2 },
                         { "name" => "f15", "type" => 2 },
                         { "name" => "f16", "type" => 2 },
#                        { "name" => "f17", "type" => 2 },
#                        { "name" => "f18", "type" => 2 },
#                        { "name" => "f19", "type" => 2 },
#                        { "name" => "f20", "type" => 2 },
#                        { "name" => "f21", "type" => 2 },
#                        { "name" => "f22", "type" => 2 },
#                        { "name" => "f23", "type" => 2 },
#                        { "name" => "f24", "type" => 2 },
#                        { "name" => "f25", "type" => 2 },
#                        { "name" => "f26", "type" => 2 },
#                        { "name" => "f27", "type" => 2 },
#                        { "name" => "f28", "type" => 2 },
#                        { "name" => "f29", "type" => 2 },
#                        { "name" => "f30", "type" => 2 },
#                        { "name" => "f31", "type" => 2 },
                         { "mode" => "mode_D" }
                       ],
  "condition"		=> [
						 { "name" => "cr0", "type" => 1 },
						 { "name" => "cr1", "type" => 1 },
						 { "name" => "cr2", "type" => 2 },
						 { "name" => "cr3", "type" => 2 },
						 { "name" => "cr4", "type" => 2 },
						 { "name" => "cr5", "type" => 1 },
						 { "name" => "cr6", "type" => 1 },
#						 { "name" => "cr7", "type" => 1 }, # => reserved for Condition Perm
						 { "mode" => "mode_P" } # real mode is 4 bit, but doesn't matter ...
					   ],
  "link"			=> [
						 { "name" => "lr", "type" => 4 }, # 3
                         { "mode" => "mode_P" }
					   ],
  "count"			=> [
						 { "name" => "ctr", "type" => 1 },
                         { "mode" => "mode_P" }
					   ]
); # %reg_classes

#--------------------------------------------------#
#                        _                         #
#                       (_)                        #
#  _ __   _____      __  _ _ __    ___  _ __  ___  #
# | '_ \ / _ \ \ /\ / / | | '__|  / _ \| '_ \/ __| #
# | | | |  __/\ V  V /  | | |    | (_) | |_) \__ \ #
# |_| |_|\___| \_/\_/   |_|_|     \___/| .__/|___/ #
#                                      | |         #
#                                      |_|         #
#--------------------------------------------------#

%nodes = (

#-----------------------------------------------------------------#
#  _       _                                         _            #
# (_)     | |                                       | |           #
#  _ _ __ | |_ ___  __ _  ___ _ __   _ __   ___   __| | ___  ___  #
# | | '_ \| __/ _ \/ _` |/ _ \ '__| | '_ \ / _ \ / _` |/ _ \/ __| #
# | | | | | ||  __/ (_| |  __/ |    | | | | (_) | (_| |  __/\__ \ #
# |_|_| |_|\__\___|\__, |\___|_|    |_| |_|\___/ \__,_|\___||___/ #
#                   __/ |                                         #
#                  |___/                                          #
#-----------------------------------------------------------------#

# commutative operations

"Add" => {
  "op_flags"  => "C",
  "irn_flags" => "R",
  "comment"   => "construct Add: Add(a, b) = Add(b, a) = a + b",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. add     %D1, %S1, %S2\t\t\t/* Add(%S1, %S2) -> %D1, (%A1, %A2) */'
},

"Addi" => {
  "op_flags"  => "c",
  "irn_flags" => "R",
  "comment"   => "construct Add: Addi(a, const) = Addi(const, a) = a + const",
  "reg_req"   => { "in" => [ "!r0" ], "out" => [ "general_purpose" ] },
#  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. addi    %D1, %S1, %C\t\t\t/* Addi(%S1, %C) -> %D1, (%A1, const) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},


"Mullw" => {
  "op_flags"  => "C",
  "irn_flags" => "R",
  "comment"   => "construct Mul: Mullw(a, b) = Mullw(b, a) = lo32(a * b)",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. mullw   %D1, %S1, %S2\t\t\t/* Mullw(%S1, %S2) -> %D1, (%A1, %A2) */'
},

"Mulhw" => {
  "op_flags"  => "C",
  "irn_flags" => "R",
  "comment"   => "construct Mul: Mulhw(a, b) = Mulhw(b, a) = hi32(a * b)",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. mulhw   %D1, %S1, %S2\t\t\t/* Mulhw(%S1, %S2) -> %D1, (%A1, %A2) */'
},

"Mulhwu" => {
  "op_flags"  => "C",
  "irn_flags" => "R",
  "comment"   => "construct Mul: Mulhwu(a, b) = Mulhwu(b, a) = hi32(a * b)",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. mulhwu  %D1, %S1, %S2\t\t\t/* Mulhwu(%S1, %S2) -> %D1, (%A1, %A2) */'
},

#"Mul_i" => {
#  "irn_flags" => "R",
#  "comment"   => "construct Mul: Mul(a, const) = Mul(const, a) = a * const",
#  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
#  "emit"      => '. mul %S1, %C, %D1\t\t\t/* signed Mul(%C, %S1) -> %D1, (%A1, const) */'
#},

"And" => {
  "op_flags"  => "C",
  "irn_flags" => "R",
  "comment"   => "construct And: And(a, b) = And(b, a) = a AND b",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. and     %D1, %S1, %S2\t\t\t/* And(%S1, %S2) -> %D1, (%A1, %A2) */'
},

#"And_i" => {
#  "irn_flags" => "R",
#  "comment"   => "construct And: And(a, const) = And(const, a) = a AND const",
#  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
#  "emit"      => '. and %S1, %C, %D1\t\t\t/* And(%C, %S1) -> %D1, (%A1, const) */'
#},

"Or" => {
  "op_flags"  => "C",
  "irn_flags" => "R",
  "comment"   => "construct Or: Or(a, b) = Or(b, a) = a OR b",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. or      %D1, %S1, %S2\t\t\t/* Or(%S1, %S2) -> %D1, (%A1, %A2) */'
},

#"Or_i" => {
#  "op_flags"  => "C",
#  "irn_flags" => "R",
#  "comment"   => "construct Or: Or(a, const) = Or(const, a) = a OR const",
#  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
#  "emit"      => '. or %S1, %C, %D1\t\t\t/* Or(%C, %S1) -> %D1, (%A1, const) */'
#},

"Xor" => {
  "op_flags"  => "C",
  "irn_flags" => "R",
  "comment"   => "construct Xor: Xor(a, b) = Xor(b, a) = a XOR b",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. xor     %D1, %S1, %S2\t\t\t/* Xor(%S1, %S2) -> %D1, (%A1, %A2) */'
},

#"Xor_i" => {
#  "irn_flags" => "R",
#  "comment"   => "construct Xor: Xor(a, const) = Xor(const, a) = a EOR const",
#  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
#  "emit"      => '. xor %S1, %C, %D1\t\t\t/* Xor(%C, %S1) -> %D1, (%A1, const) */'
#},

# not commutative operations

"Sub" => {
  "irn_flags" => "R",
  "comment"   => "construct Sub: Sub(a, b) = a - b",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. sub     %D1, %S1, %S2\t\t\t/* Sub(%S1, %S2) -> %D1, (%A1, %A2) */'
},

#"Sub_i" => {
#  "irn_flags" => "R",
#  "comment"   => "construct Sub: Sub(a, const) = a - const",
#  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
#  "emit"      => '. subl %S1, %C, %D1\t\t\t/* Sub(%S1, %C) -> %D1, (%A1, const) */'
#},

"Slw" => {
  "irn_flags" => "R",
  "comment"   => "construct Shl: Shl(a, b) = a << b",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. slw     %D1, %S1, %S2\t\t\t/* Shl(%S1, %S2) -> %D1, (%A1, %A2) */'
},

#"Shl_i" => {
#  "irn_flags" => "R",
#  "comment"   => "construct Shl: Shl(a, const) = a << const",
#  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
#  "emit"      => '. shl %S1, %C, %D1\t\t\t/* Shl(%S1, %C) -> %D1, (%A1, const) */'
#},

"Srw" => {
  "irn_flags" => "R",
  "comment"   => "construct Shr: Srw(a, b): c = a >> b",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. srw     %D1, %S1, %S2\t\t\t/* Srw(%S1, %S2) -> %D1, (%A1, %A2) */'
},

#"Shr_i" => {
#  "irn_flags" => "R",
#  "comment"   => "construct Shr: Shr(a, const) = a >> const",
#  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
#  "emit"      => '. shr %S1, %C, %D1\t\t\t/* Shr(%S1, %C) -> %D1, (%A1, const) */'
#},

"Sraw" => {
  "irn_flags" => "R",
  "comment"   => "construct Shrs: Sraw(a, b): c = a >> b",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. sraw    %D1, %S1, %S2\t\t\t/* Sraw(%S1, %S2) -> %D1, (%A1, %A2) */'
},

"Srawi" => {
  "irn_flags" => "R",
  "comment"   => "construct Shrs: Srawi(a, const): c = a >> const",
  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. sraw    %D1, %S1, %C\t\t\t/* Sraw(%S1, %C) -> %D1, (%A1, const) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'

},

"Rlwnm" => {
  "irn_flags" => "R",
  "comment"   => "construct ???: Rlwnm(a, b): c = a ROTL b",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. rlwnm   %D1, %S1, %S2\t\t\t/* Rlwnm(%S1, %S2) -> %D1, (%A1, %A2) */'
},

"Rlwinm" => {
  "irn_flags" => "R",
  "comment"   => "construct ???: Rlwinm(a, b_const, c_const, d_const): (m = MASK(c, d)) e = (a ROTL b) & m",
  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. rlwinm  %D1, %S1, %ppc32_rlwimi_emit_helper\t\t\t/* Rlwinm(%S1, %ppc32_rlwimi_emit_helper) -> %D1, (%A1) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},


"Neg" => {
  "irn_flags" => "R",
  "comment"   => "construct Minus: Neg(a) = -a",
  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. neg     %D1, %S1\t\t\t/* Neg(%S1) -> %D1, (%A1) */'
},

"Not" => {
  "irn_flags" => "R",
  "comment"   => "construct Not: Not(a) = !a",
  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. nor     %D1, %S1, %S1\t\t\t/* Not(%S1) -> %D1, (%A1) */'
},

"Extsb" => {
  "irn_flags" => "R",
  "comment"   => "construct Sign extension of byte: Extsb(char a) = (int) a",
  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. extsb   %D1, %S1\t\t\t/* Extsb(%S1) -> %D1, (%A1) */'
},

"Extsh" => {
  "irn_flags" => "R",
  "comment"   => "construct Sign extension of halfword: Extsh(char a) = (short) a",
  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. extsh   %D1, %S1\t\t\t/* Extsh(%S1) -> %D1, (%A1) */'
},

"Divw" => {
  "irn_flags" => "R",
  "comment"   => "construct Div (signed): Div(a, b) = a div b",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. divw    %D1, %S1, %S2\t\t\t/* Div(%S1, %S2) -> %D1, (%A1, %A2) */'
},

"Divwu" => {
  "irn_flags" => "R",
  "comment"   => "construct Div (unsigned): Div(a, b) = a div b",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "general_purpose" ] },
  "emit"      => '. divwu   %D1, %S1, %S2\t\t\t/* Div(%S1, %S2) -> %D1, (%A1, %A2) */'
},

"Mtctr" => {
  "irn_flags" => "R",
  "comment"   => "construct Mtctr: Ctr = a",
  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "count" ] },
  "emit"      => '. mtctr   %S1\t\t\t/* Mtctr(%S1) -> %D1, (%A1) */'
},


# other operations

"Const" => {
  "op_flags"  => "c",
  "irn_flags" => "R",
  "comment"   => "Const (high-level node)",
  "reg_req"   => { "out" => [ "general_purpose" ] },
  "cmp_attr"  =>
'
	return attr_a->constant_tarval!=attr_b->constant_tarval;
'
},

"fConst" => {
  "op_flags"  => "c",
  "irn_flags" => "R",
  "comment"   => "float Const (high-level node)",
  "reg_req"   => { "out" => [ "floating_point" ] },
  "cmp_attr"  =>
'
	return attr_a->constant_tarval!=attr_b->constant_tarval;
'
},

"SymConst" => {
  "op_flags"  => "c",
  "irn_flags" => "R",
  "comment"   => "SymConst (high-level node)",
  "reg_req"   => { "out" => [ "general_purpose" ] },
  "cmp_attr"  =>
'
	return attr_a->constant_tarval!=attr_b->constant_tarval;
'
},

"Unknown" => {
  "op_flags"  => "c",
  "irn_flags" => "R",
  "comment"   => "construct unknown register",
  "reg_req"   => { "out" => [ "general_purpose" ] },
  "emit"      => '. \t\t\t\t\t/* use %D1 as uninitialized value */',
  "cmp_attr"  =>
'
	return 1;
'
},

"fUnknown" => {
  "op_flags"  => "c",
  "irn_flags" => "R",
  "comment"   => "construct unknown float register",
  "reg_req"   => { "out" => [ "floating_point" ] },
  "emit"      => '. \t\t\t\t\t/* use %D1 as uninitialized value */',
  "cmp_attr"  =>
'
	return 1;
'
},

"cUnknown" => {
  "op_flags"  => "c",
  "irn_flags" => "R",
  "comment"   => "construct unknown condition register",
  "reg_req"   => { "out" => [ "condition" ] },
  "emit"      => '. \t\t\t\t\t/* use %D1 as uninitialized value */',
  "cmp_attr"  =>
'
	return 1;
'
},

"Addi_zero" => {
  "op_flags"  => "c",
  "irn_flags" => "R",
  "comment"   => "load constant (16bit with sign extension)",
  "reg_req"   => { "out" => [ "general_purpose" ] },
  "emit"      => '. addi    %D1, 0, %C\t\t\t/* lower 16 bit of %C (sign extended) -> %D1 */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Branch" => {
  "op_flags"  => "L|X|Y",
  "comment"   => "branch somewhere",
  "reg_req"   => { "in" => [ "condition" ], "out" => [ "none", "none" ] },
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"LoopCopy" => {
  "irn_flags" => "R",
  "comment"   => "construct LoopCopy(src, dest, count, mem): Copy count words from src to dest",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose", "count", "none" ], "out" => [ "none", "in_r1", "in_r2", "in_r3", "general_purpose" ] },
},

"Switch" => {
  "op_flags" => "L|X|Y",
  "comment"   => "construct Switch(selector): Jump to whatever",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose", "condition" ], "out" => [ "none" ] },
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Addis_zero" => {
  "op_flags"  => "c",
  "irn_flags" => "R",
  "comment"   => "load the constant to higher 16 bit of register",
  "reg_req"   => { "out" => [ "general_purpose" ] },
  "emit"      => '. addis   %D1, 0, %C\t\t\t/* %C << 16 -> %D1 */',
  "attr"      => "ppc32_attr_offset_mode om, tarval *tv, ident *id",
  "init_attr" =>
'
	attr->offset_mode = om;
	if (tv) {
		attr->content_type = ppc32_ac_Const;
		attr->constant_tarval = tv;
	}
	else if (id) {
		attr->content_type = ppc32_ac_SymConst;
		attr->symconst_ident = id;
	}
',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Ori" => {
  "op_flags"  => "c",
  "irn_flags" => "R",
  "comment"   => "ors constant with register",
  "reg_req"   => { "in" => [ "general_purpose"], "out" => [ "general_purpose" ] },
  "emit"      => '. ori     %D1, %S1, %C\t\t\t/* Ori(%S1,%C) -> %D1 */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Andi_dot" => {
  "op_flags"  => "c",
  "irn_flags" => "R",
  "comment"   => "ands constant with register with cr0 update",
  "reg_req"   => { "in" => [ "general_purpose"], "out" => [ "general_purpose", "cr0" ] },
  "emit"      => '. andi.   %D1, %S1,%C\t\t\t/* Andi(%S1,%C) -> %D1 (%D2 changed) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Cmp" => {
  "irn_flags" => "R",
  "comment"   => "construct Cmp: Cmp(a, b) = Flags in crX",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "condition" ] },
  "emit"      => '. cmp     %D1, 0, %S1, %S2\t\t\t/* Cmp(%S1, %S2) -> %D1, (%A1, %A2) */'
},

"Cmpi" => {
  "irn_flags" => "R",
  "comment"   => "construct Cmp immediate: Cmpi(a, const) = Flags in crX",
  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "condition" ] },
  "emit"      => '. cmpi    %D1, 0, %S1, %C\t\t\t/* Cmpi(%S1, %C) -> %D1, (%A1) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},


"Cmpl" => {
  "irn_flags" => "R",
  "comment"   => "construct Cmp logical: Cmpl(a, b) = Flags in crX",
  "reg_req"   => { "in" => [ "general_purpose", "general_purpose" ], "out" => [ "condition" ] },
  "emit"      => '. cmpl    %D1, 0, %S1, %S2\t\t\t/* Cmpl(%S1, %S2) -> %D1, (%A1, %A2) */'
},

"Cmpli" => {
  "irn_flags" => "R",
  "comment"   => "construct Cmp logical immediate: Cmpli(a, const) = Flags in crX",
  "reg_req"   => { "in" => [ "general_purpose" ], "out" => [ "condition" ] },
  "emit"      => '. cmpli   %D1, 0, %S1, %C\t\t\t/* Cmpli(%S1, %C) -> %D1, (%A1) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},


# Load / Store

"Lbz" => {
  "op_flags"  => "L|F",
  "irn_flags" => "R",
  "state"     => "exc_pinned",
  "comment"   => "construct Load (byte unsigned): Load(ptr, mem) = LD ptr -> reg",
  "reg_req"   => { "in" => [ "!r0", "none" ], "out" => [ "general_purpose" ] },
  "emit"      => '. lbz     %D1, %O(%S1)\t\t\t/* Load(%O(%S1)) -> %D1, (%A1) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Lhz" => {
  "op_flags"  => "L|F",
  "irn_flags" => "R",
  "state"     => "exc_pinned",
  "comment"   => "construct Load (halfword unsigned): Load(ptr, mem) = LD ptr -> reg",
  "reg_req"   => { "in" => [ "!r0", "none" ], "out" => [ "general_purpose" ] },
  "emit"      => '. lhz     %D1, %O(%S1)\t\t\t/* Load(%O(%S1)) -> %D1, (%A1) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Lha" => {
  "op_flags"  => "L|F",
  "irn_flags" => "R",
  "state"     => "exc_pinned",
  "comment"   => "construct Load (halfword signed): Load(ptr, mem) = LD ptr -> reg",
  "reg_req"   => { "in" => [ "!r0", "none" ], "out" => [ "general_purpose" ] },
  "emit"      => '. lha     %D1, %O(%S1)\t\t\t/* Load(%O(%S1)) -> %D1, (%A1) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Lwz" => {
  "op_flags"  => "L|F",
  "irn_flags" => "R",
  "state"     => "exc_pinned",
  "comment"   => "construct Load (word): Load(ptr, mem) = LD ptr -> reg",
  "reg_req"   => { "in" => [ "!r0", "none" ], "out" => [ "general_purpose" ] },
  "emit"      => '. lwz     %D1, %O(%S1)\t\t\t/* Load(%O(%S1)) -> %D1, (%A1) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Lwzu" => {
  "op_flags"  => "L|F",
  "irn_flags" => "R",
  "state"     => "exc_pinned",
  "comment"   => "construct Load with update (word): Load(ptr, mem) = LD ptr -> reg",
  "reg_req"   => { "in" => [ "!r0", "none" ], "out" => [ "general_purpose", "in_r1"] },
  "emit"      => '. lwzu    %D1, %O(%S1)\t\t\t/* Load(%O(%S1)) -> %D1, %S1 += %O, (%A1) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Stb" => {
  "op_flags"  => "L|F",
  "state"     => "exc_pinned",
  "comment"   => "construct Store: Store (byte) (ptr, val, mem) = ST ptr,val",
  "reg_req"   => { "in" => [ "!r0", "general_purpose", "none" ] },
  "emit"      => '. stb     %S2, %O(%S1)\t\t\t/* Store(%S2) -> (%S1), (%A1, %A2) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Sth" => {
  "op_flags"  => "L|F",
  "state"     => "exc_pinned",
  "comment"   => "construct Store: Store (halfword) (ptr, val, mem) = ST ptr,val",
  "reg_req"   => { "in" => [ "!r0", "general_purpose", "none" ] },
  "emit"      => '. sth     %S2, %O(%S1)\t\t\t/* Store(%S2) -> (%S1), (%A1, %A2) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Stw" => {
  "op_flags"  => "L|F",
  "state"     => "exc_pinned",
  "comment"   => "construct Store: Store (word) (ptr, val, mem) = ST ptr,val",
  "reg_req"   => { "in" => [ "!r0", "general_purpose", "none" ] },
  "emit"      => '. stw     %S2, %O(%S1)\t\t\t/* Store(%S2) -> (%S1), (%A1, %A2) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

#--------------------------------------------------------#
#    __ _             _                     _            #
#   / _| |           | |                   | |           #
#  | |_| | ___   __ _| |_   _ __   ___   __| | ___  ___  #
#  |  _| |/ _ \ / _` | __| | '_ \ / _ \ / _` |/ _ \/ __| #
#  | | | | (_) | (_| | |_  | | | | (_) | (_| |  __/\__ \ #
#  |_| |_|\___/ \__,_|\__| |_| |_|\___/ \__,_|\___||___/ #
#--------------------------------------------------------#

# commutative operations

"fAdd" => {
  "op_flags"  => "C",
  "irn_flags" => "R",
  "comment"   => "construct FP Add: Add(a, b) = Add(b, a) = a + b",
  "reg_req"   => { "in" => [ "floating_point", "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fadd    %D1, %S1, %S2\t\t\t/* FP Add(%S1, %S2) -> %D1 */'
},

"fAdds" => {
  "op_flags"  => "C",
  "irn_flags" => "R",
  "comment"   => "construct FP Add (single): Add(a, b) = Add(b, a) = a + b",
  "reg_req"   => { "in" => [ "floating_point", "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fadds   %D1, %S1, %S2\t\t\t/* FP Add(%S1, %S2) -> %D1 */'
},

"fMul" => {
  "op_flags"  => "C",
  "comment"   => "construct FP Mul: Mul(a, b) = Mul(b, a) = a * b",
  "reg_req"   => { "in" => [ "floating_point", "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fmul    %D1, %S1, %S2\t\t\t/* FP Mul(%S1, %S2) -> %D1 */'
},

"fMuls" => {
  "op_flags"  => "C",
  "comment"   => "construct FP Mul (single): Mul(a, b) = Mul(b, a) = a * b",
  "reg_req"   => { "in" => [ "floating_point", "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fmuls   %D1, %S1, %S2\t\t\t/* FP Mul(%S1, %S2) -> %D1 */'
},

"fNeg" => {
  "comment"   => "construct FP Negation: fNeg(a) = -a",
  "reg_req"   => { "in" => [ "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fneg    %D1, %S1\t\t\t/* FP fNeg(%S1) -> %D1 */'
},


"fMax" => {
  "op_flags"  => "C",
  "irn_flags" => "R",
  "comment"   => "construct FP Max: Max(a, b) = Max(b, a) = a > b ? a : b",
  "reg_req"   => { "in" => [ "floating_point", "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fmax    %S1, %S2, %D1\t\t\t/* FP Max(%S1, %S2) -> %D1 */'
},

"fMin" => {
  "op_flags"  => "C",
  "irn_flags" => "R",
  "comment"   => "construct FP Min: Min(a, b) = Min(b, a) = a < b ? a : b",
  "reg_req"   => { "in" => [ "floating_point", "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fmin    %S1, %S2, %D1\t\t\t/* FP Min(%S1, %S2) -> %D1 */'
},

# not commutative operations

"fSub" => {
  "irn_flags" => "R",
  "comment"   => "construct FP Sub: Sub(a, b) = a - b",
  "reg_req"   => { "in" => [ "floating_point", "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fsub    %D1, %S1, %S2\t\t\t/* FP Sub(%S1, %S2) -> %D1 */'
},

"fSubs" => {
  "irn_flags" => "R",
  "comment"   => "construct FP Sub (single): Sub(a, b) = a - b",
  "reg_req"   => { "in" => [ "floating_point", "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fsub    %D1, %S1, %S2\t\t\t/* FP Sub(%S1, %S2) -> %D1 */'
},

"fDiv" => {
  "comment"   => "construct FP Div: Div(a, b) = a / b",
  "reg_req"   => { "in" => [ "floating_point", "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fdiv    %D1, %S1, %S2\t\t\t/* FP Div(%S1, %S2) -> %D1 */'
},

"fDivs" => {
  "comment"   => "construct FP Div (single): Div(a, b) = a / b",
  "reg_req"   => { "in" => [ "floating_point", "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fdivs   %D1, %S1, %S2\t\t\t/* FP Div(%S1, %S2) -> %D1 */'
},

"fMinus" => {
  "irn_flags" => "R",
  "comment"   => "construct FP Minus: fMinus(a) = -a",
  "reg_req"   => { "in" => [ "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fneg    %D1, %S1\t\t\t/* FP fMinus(%S1) -> %D1 */'
},

"fCtiw" => {
  "irn_flags" => "R",
  "comment"   => "construct FP Convert to integer word: fCtiw(a) = (int) a",
  "reg_req"   => { "in" => [ "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fctiw   %D1, %S1\t\t\t/* FP fCtiw(%S1) -> %D1 */'
},

"fRsp" => {
  "irn_flags" => "R",
  "comment"   => "construct FP Round to single: fRsp(a) = (float) a",
  "reg_req"   => { "in" => [ "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. frsp    %D1, %S1\t\t\t/* FP fRsp(%S1) -> %D1 */'
},

"fAbs" => {
  "irn_flags" => "R",
  "comment"   => "construct FP Abs: fAbs(a) = |a|",
  "reg_req"   => { "in" => [ "floating_point" ], "out" => [ "floating_point" ] },
  "emit"      => '. fabs    %D1, %S1\t\t\t/* FP fAbs(%S1) -> %D1 */'
},

"fCmpu" => {
  "irn_flags" => "R",
  "comment"   => "construct FP Cmp unordered: fCmpu(a, b) = a ? b",
  "reg_req"   => { "in" => [ "floating_point", "floating_point" ], "out" => [ "condition" ] },
  "emit"      => '. fcmpu   %D1, %S1, %S2\t\t\t/* FP fCmpu(%S1, %S2) -> %D1 */'
},

# other operations

#"fConst" => {
#  "op_flags"  => "c",
#  "irn_flags" => "R",
#  "comment"   => "represents a FP constant",
#  "reg_req"   => { "out" => [ "floating_point" ] },
#  "emit"      => '. fmov %C, %D1\t\t\t/* Mov fConst into register */',
#  "cmp_attr"  =>
#'
#	/* TODO: compare fConst attributes */
#	return 1;
#'
#},

"fUnknown" => {
  "op_flags"  => "c",
  "irn_flags" => "R",
  "comment"   => "construct unknown floating point register",
  "reg_req"   => { "out" => [ "floating_point" ] },
  "emit"      => '. \t\t\t\t\t/* use %D1 as uninitialized value */',
  "cmp_attr"  =>
'
	return 1;
'
},

# Load / Store

"Lfd" => {
  "op_flags"  => "L|F",
  "irn_flags" => "R",
  "state"     => "exc_pinned",
  "comment"   => "construct FP Load (double): Load(ptr, mem) = LD ptr",
  "reg_req"   => { "in" => [ "!r0", "none" ], "out" => [ "floating_point" ] },
  "emit"      => '. lfd     %D1, %O(%S1)\t\t\t/* Load(%O(%S1)) -> %D1 */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Lfs" => {
  "op_flags"  => "L|F",
  "irn_flags" => "R",
  "state"     => "exc_pinned",
  "comment"   => "construct FP Load (single): Load(ptr, mem) = LD ptr",
  "reg_req"   => { "in" => [ "!r0", "none" ], "out" => [ "floating_point" ] },
  "emit"      => '. lfs     %D1, %O(%S1)\t\t\t/* Load(%O(%S1)) -> %D1 */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Stfd" => {
  "op_flags"  => "L|F",
  "state"     => "exc_pinned",
  "comment"   => "construct Store (double): Store(ptr, val, mem)  = ST ptr,val",
  "reg_req"   => { "in" => [ "!r0", "floating_point", "none" ] },
  "emit"      => '. stfd    %S2, %O(%S1)\t\t\t/* Store(%S2) -> (%S1), (%A1, %A2) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

"Stfs" => {
  "op_flags"  => "L|F",
  "state"     => "exc_pinned",
  "comment"   => "construct Store (single): Store(ptr, val, mem)  = ST ptr,val",
  "reg_req"   => { "in" => [ "!r0", "floating_point", "none" ] },
  "emit"      => '. stfs    %S2, %O(%S1)\t\t\t/* Store(%S2) -> (%S1), (%A1, %A2) */',
  "cmp_attr"  =>
'
	return (attr_a->constant_tarval!=attr_b->constant_tarval);
'
},

); # end of %nodes
