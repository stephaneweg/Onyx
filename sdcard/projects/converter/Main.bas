' Main.bas -- what the converter does (the window itself: Main.form)

SUB Main_Load
  status.Text = "Type a temperature in Celsius"
  celsius.Focus
END SUB

SUB convert_Click
  DIM c AS DOUBLE, f AS DOUBLE
  c = VAL(celsius.Text)
  f = c * 9 / 5 + 32
  fahrenheit.Text = STR$(f)
  scale.Value = INT(f)
  status.Text = "Converted at " + TIME$
END SUB

SUB celsius_Change
  IF live.Checked THEN convert_Click
END SUB

SUB wipe_Click
  celsius.Text = "": fahrenheit.Text = ""
  status.Text = "Ready"
END SUB

SUB mnuCopy_Click
  SETCLIPBOARD fahrenheit.Text
END SUB

SUB mnuAbout_Click
  r = MSGBOX("About Converter", "Celsius to Fahrenheit, made with QBStudio.")
END SUB

SUB mnuQuit_Click
  Main.Close
END SUB
