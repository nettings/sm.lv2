# sm.lv2
A simple **s**peaker **m**anagement LV2 plugin with global master volume
and per-channel trim, delay, and low-shelf.

This plugin lets you optimize a stereo or multichannel speaker system to
your listening environment.

In most real-life rooms, you will not be able to position the speakers
according to a perfect stereo triangle or ITU circle. By carefully applying
per-channel attenuation and delay, you can restore almost perfect
reproduction at the listening spot.

For example: you have a 5.1 system, but your listening sofa is against the
back wall, and the surrounds on that same wall. This means they will be
louder at your ears and dominate localisation, because their sound will
reach you earlier than the LCR speakers. 
Measure the distance to the speaker that's furthest away from you. This is
the delay reference (it gets no delay at all). Say it's the left speaker.
Now measure the distance to the centre speaker and compute the distance
difference. Say it's 30 cm closer to your listening position. Sound travels
30 cm in one millisecond, so if you delay it by one millisecond, it will
arrive in-phase with the left speaker at your ear.
The right speaker is 10 cm closer, that means it should get roughly 0.33 ms
delay.
The surrounds are 3 m closer, so they need 9 ms delay. For 5.1, it is
recommended to have them hit another 10ms later than the front, for a total
delay of 19 ms (unless your amplifier already does that).

A speaker that is closer is also louder, you can compensate that by ear
using the per-channel trims.
If you place a speaker next to a wall, this will boost the bass compared to
a free-standing speaker by about 3dB. If it's in a room edge, the boost is
6dB. If it's in a room corner, it will be 9 dB. This can be corrected with
the per-channel low shelves.

A precise calibration procedure including test tones will be added to this
README in due course, which will allow you to align your speakers to a few
samples and less than one dB  precision just by using the amazing capabilities of the human
hearing system.


