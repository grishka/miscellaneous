package me.grishka.gbemulator.test;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

@DisplayName("Timer/divider")
public class TimerTests{
	private static final String DIR="ROMs/mts-20221022-1430-8d742b9/acceptance/timer/";

	private void doTest(String name){
		assertArrayEquals(TestUtils.MTS_PASSED, TestUtils.runMooneyeTestRomAndGetSerialOutput(DIR+name+".gb"));
	}


	@Test
	@DisplayName("DIV write")
	public void testDivWrite(){
		doTest("div_write");
	}

	@Test
	@DisplayName("Rapid toggle")
	public void testRapidToggle(){
		doTest("rapid_toggle");
	}

	@Test
	@DisplayName("tim00 div trigger")
	public void testTim00DivTrigger(){
		doTest("tim00_div_trigger");
	}

	@Test
	@DisplayName("tim00")
	public void testTim00(){
		doTest("tim00");
	}

	@Test
	@DisplayName("tim01 div trigger")
	public void testTim01DivTrigger(){
		doTest("tim01_div_trigger");
	}

	@Test
	@DisplayName("tim01")
	public void testTim01(){
		doTest("tim01");
	}

	@Test
	@DisplayName("tim10 div trigger")
	public void testTim10DivTrigger(){
		doTest("tim10_div_trigger");
	}

	@Test
	@DisplayName("tim10")
	public void testTim10(){
		doTest("tim10");
	}

	@Test
	@DisplayName("tim11 div trigger")
	public void testTim11DivTrigger(){
		doTest("tim11_div_trigger");
	}

	@Test
	@DisplayName("tim11")
	public void testTim11(){
		doTest("tim11");
	}

	@Test
	@DisplayName("TIMA reload")
	public void testTimaReload(){
		doTest("tima_reload");
	}

	@Test
	@DisplayName("TIMA write while reloading")
	public void testTimaWriteReloading(){
		doTest("tima_write_reloading");
	}

	@Test
	@DisplayName("TMA write while reloading")
	public void testTmaWriteReloading(){
		doTest("tma_write_reloading");
	}
}
