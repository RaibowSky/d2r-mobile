package org.d2r.binaryprobe;

/** Combine independent on-screen keys with all fingers on the rhythm surface. */
final class RhythmInputState {
    private long keys;
    private int fingers;
    void key(long mask,boolean down){if(down)keys|=mask;else keys&=~mask;}
    void fingers(int count){if(count<0||count>16)throw new IllegalArgumentException("touch count");fingers=count;}
    long buttons(){return keys|(fingers>0?1L:0L);}
    void reset(){keys=0;fingers=0;}
}
