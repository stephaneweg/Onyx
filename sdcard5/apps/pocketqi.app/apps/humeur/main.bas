' humeur -- l'humeur a deux axes : valence (negatif <-> positif) et eveil (calme <-> excite), de -100 a 100
SetMood 80, 20
Say "Je suis " + MoodName$ + "."
SetMood -70, 60
Say "Et la, " + MoodName$ + " :" + STR$(MoodValence) + "," + STR$(MoodArousal) + "."
SetMood 0, -90
Say "Puis " + MoodName$ + "."
SetMoodName "happy", 2
Say "Enfin " + MoodName$ + " :" + STR$(MoodValence) + "," + STR$(MoodArousal) + "."
