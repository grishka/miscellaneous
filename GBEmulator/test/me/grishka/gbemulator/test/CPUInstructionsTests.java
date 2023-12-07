package me.grishka.gbemulator.test;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

@DisplayName("CPU instructions")
public class CPUInstructionsTests{
	private void doTest(String romName){
		assertEquals(romName+"\n\n\nPassed\n", TestUtils.runBlarggTestRomAndGetSerialOutput("ROMs/cpu_instrs/individual/"+romName+".gb"));
	}

	@Test
	@DisplayName("Special")
	public void testSpecial(){
		doTest("01-special");
	}

	@Test
	@DisplayName("Interrupts")
	public void testInterrupts(){
		doTest("02-interrupts");
	}

	@Test
	@DisplayName("op sp, hl")
	public void testOpSpHl(){
		doTest("03-op sp,hl");
	}

	@Test
	@DisplayName("op r, imm")
	public void testOpRImm(){
		doTest("04-op r,imm");
	}

	@Test
	@DisplayName("op rp")
	public void testOpRp(){
		doTest("05-op rp");
	}

	@Test
	@DisplayName("ld r, r")
	public void testLdRR(){
		doTest("06-ld r,r");
	}

	@Test
	@DisplayName("jr, jp, call, ret, rst")
	public void testFlowControl(){
		doTest("07-jr,jp,call,ret,rst");
	}

	@Test
	@DisplayName("Misc instructions")
	public void testMiscInstrs(){
		doTest("08-misc instrs");
	}

	@Test
	@DisplayName("op r, r")
	public void testOpRR(){
		doTest("09-op r,r");
	}

	@Test
	@DisplayName("Bit operations")
	public void testBitOps(){
		doTest("10-bit ops");
	}

	@Test
	@DisplayName("op a, (hl)")
	public void testOpAHl(){
		doTest("11-op a,(hl)");
	}
}
