' Main.bas -- what Pages does: its window (Main.form) has two Hosts, `side` and `page`; each shows a
' user control (Sidebar.form, Home.form, Settings.form). host.Content = Name puts one in place of another.

DIM SHARED visits

SUB goHome_Click
  page.Content = Home
  status.Text = "Home"
END SUB

SUB goSettings_Click
  page.Content = Settings
END SUB

' (a user control's events: <Name>_Load once, when it is made; <Name>_Show each time a Host shows it)
SUB Settings_Show
  visits = visits + 1
  status.Text = "Settings (shown" + STR$(visits) + " times)"
END SUB

SUB volume_Change
  level.Value = volume.Value
END SUB

SUB hello_Click
  IF who.Text = "" THEN greeting.Text = "Hello!" ELSE greeting.Text = "Hello, " + who.Text + "!"
END SUB

SUB leave_Click
  Main.Close
END SUB
