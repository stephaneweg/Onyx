# pstest.S -- the Gekko's paired singles and quantized loads / stores, with the results the
# manual gives (gctest ps checks them: expected words at the end, from "expect").
	.text
	.globl run_ps
run_ps:
	# r3 = the output buffer; data below
	lis 4, data@ha
	addi 4, 4, data@l
	# GQR1: load u8 scale 2 (x / 4), store s16 scale 3 (x * 8)
	lis 5, 0x0204
	ori 5, 5, 0x0307
	mtspr 913, 5
	# GQR0 = 0: floats
	li 5, 0
	mtspr 912, 5
	psq_l 1, 0(4), 0, 1		# u8 8, 12 -> 2.0, 3.0
	psq_l 2, 4(4), 0, 0		# floats 1.5, -0.25
	ps_add 3, 1, 2			# 3.5, 2.75
	ps_mul 4, 1, 2			# 3.0, -0.75
	ps_madd 5, 1, 2, 3		# 2*1.5+3.5 = 6.5, 3*-0.25+2.75 = 2.0
	ps_sum0 6, 1, 2, 3		# (frD, frA, frC, frB) ps0 = A0 + B1 = 2 + 2.75, ps1 = C1 = -0.25
	ps_sum1 7, 1, 2, 3		# ps0 = C0 = 1.5, ps1 = A0 + B1 = 4.75
	ps_merge10 8, 1, 2		# 3.0, 1.5
	ps_muls1 9, 2, 1		# 1.5*3 = 4.5, -0.25*3 = -0.75
	ps_neg 10, 2			# -1.5, 0.25
	psq_st 3, 0(3), 0, 0		# floats 3.5, 2.75
	psq_st 4, 8(3), 0, 1		# s16 * 8: 24, -6
	psq_st 5, 12(3), 1, 0		# only ps0: 6.5
	psq_st 6, 16(3), 0, 0
	psq_st 7, 24(3), 0, 0
	psq_st 8, 32(3), 0, 0
	psq_st 9, 40(3), 0, 0
	psq_st 10, 48(3), 0, 0
	ps_cmpo0 2, 1, 2		# 2.0 > 1.5: GT in cr2
	mfcr 5
	stw 5, 56(3)
	li 3, 15
	blr
	.align 3
data:
	.byte 8, 12, 0, 0
	.float 1.5, -0.25
