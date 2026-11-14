from textual.app import App
from textual.screen import Screen
from textual.containers import Vertical
from textual.widgets import Input, Button, Label, Select, ProgressBar, Static


class Welcome(Screen):
    def compose(self):
        yield Vertical(
            Static(" MY OS ", id="logo"),
            Label("Welcome! Let's set up your system."),
            Button("Start", id="start", variant="primary"),
            classes="box",
        )

    def on_button_pressed(self, event):
        self.app.push_screen(Account())


class Account(Screen):
    def compose(self):
        yield Vertical(
            Static("Create Account", classes="title"),
            Label("Username"),
            Input(placeholder="your name", id="user"),
            Label("Password"),
            Input(placeholder="secret", password=True, id="pw"),
            Label("Install on"),
            Select(
                [("Disk 1 (fake)", "Disk 1"), ("Disk 2 (fake)", "Disk 2")],
                allow_blank=False,
                id="disk",
            ),
            Button("Next", id="next", variant="success"),
            classes="box",
        )

    def on_button_pressed(self, event):
        self.app.user = self.query_one("#user", Input).value or "(none)"
        self.app.disk = self.query_one("#disk", Select).value
        self.app.push_screen(Confirm())


class Confirm(Screen):
    def compose(self):
        yield Vertical(
            Static("Confirm", classes="title"),
            Label(f"User: {self.app.user}"),
            Label(f"Disk: {self.app.disk}"),
            Label(" This will ERASE the disk!", id="warn"),
            Button("Install", id="go", variant="error"),
            Button("Back", id="back"),
            classes="box",
        )

    def on_button_pressed(self, event):
        if event.button.id == "go":
            self.app.push_screen(Installing())
        else:
            self.app.pop_screen()


class Installing(Screen):
    def compose(self):
        yield Vertical(
            Static("Installing...", classes="title"),
            ProgressBar(total=100, id="bar"),
            Label("", id="status"),
            Button("Finish", id="done", variant="primary", disabled=True),
            classes="box",
        )

    def on_mount(self):
        self.timer = self.set_interval(0.1, self.tick)

    def tick(self):
        bar = self.query_one("#bar", ProgressBar)
        bar.advance(2)
        if bar.progress >= 100:
            self.timer.stop()
            self.query_one("#status", Label).update(" Done!")
            self.query_one("#done", Button).disabled = False

    def on_button_pressed(self, event):
        self.app.exit()


class InstallerApp(App):
    user = ""
    dddddddddddddddddddddddddisk = ""

    CSS = """
    Screen {
        align: center middle;
        background: #0f172a;
    }
    .box {
        width: 90%;
        height: auto;
        border: round #38bdf8;
        padding: 1 2;
        background: #1e293b;
    }
    #logo {
        text-align: center;
        color: #38bdf8;
        text-style: bold;
        padding-bottom: 1;
    }
    .title {
        text-align: center;
        color: #facc15;
        text-style: bold;
        padding-bottom: 1;
    }
    #warn { color: #f87171; padding: 1 0; }
    Button { margin-top: 1; width: 100%; }
    """

    def on_mount(self):
        self.push_screen(Welcome())


InstallerApp().run()
