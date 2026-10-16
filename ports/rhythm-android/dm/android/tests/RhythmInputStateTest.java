package org.d2r.binaryprobe;
public final class RhythmInputStateTest {
    private static void check(boolean value){if(!value)throw new AssertionError();}
    public static void main(String[] args){
        RhythmInputState s=new RhythmInputState();
        s.fingers(2);s.fingers(1);check(s.buttons()==1); // One finger releases, other still holds A.
        s.key(2,true);s.fingers(0);check(s.buttons()==2); // Surface release preserves B.
        s.key(1,true);s.fingers(1);s.key(1,false);check(s.buttons()==3); // Key release preserves surface A.
        s.reset();check(s.buttons()==0); // Pause, dialog and mode changes release every source.
        s.fingers(1);s.reset();s.key(1L<<15,true);check(s.buttons()==1L<<15);
        System.out.println("PASS rhythm input: independent sources, multitouch release, lifecycle reset");
    }
}
