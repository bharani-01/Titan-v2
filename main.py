import tkinter as tk
from tkinter import ttk
import tkintermapview
import math
import random
from datetime import datetime

# --- TACTICAL INDUSTRIAL THEME ---
C_BG      = "#0B0C0D"  # Obsidian Black
C_PANEL   = "#16181D"  # Slate Panel
C_ACCENT  = "#FFB400"  # Cyber Amber
C_STATUS  = "#50FA7B"  # Emerald Green
C_DANGER  = "#FF5555"  # Crimson Red
C_TEXT    = "#D1D1D1"  # Platinum Grey

# --- TYPOGRAPHY ---
F_HEAD    = ("Verdana", 20, "bold")
F_SUB     = ("Verdana", 11, "bold")
F_DIGIT   = ("Courier New", 32, "bold")
F_UI      = ("Verdana", 9)

class ARES_CommandCenter(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("ARES-V1 | STRATEGIC ROVER CONTROL")
        self.state('zoomed')
        self.configure(bg=C_BG)

        # 1. GLOBAL STATE & CONFIG
        self.config = {
            "failsafe": tk.StringVar(value="Return to Home"),
            "logging": tk.BooleanVar(value=True),
            "network": {
                "cam_ip": "192.168.1.50",
                "ws_url": "ws://192.168.1.100:81"
            },
            "mission_logs": [
                {"time": "12:00:01", "lvl": "INFO", "src": "CORE", "event": "System Init", "data": "OK"},
                {"time": "12:01:45", "lvl": "WARN", "src": "SONAR", "event": "Proximity Alert", "data": "28cm"},
                {"time": "12:02:10", "lvl": "CRIT", "src": "MOTOR", "event": "Stall Detected", "data": "Axle 4"},
                {"time": "12:05:30", "lvl": "INFO", "src": "ARM", "event": "Joint Calibration", "data": "DONE"}
            ]
        }

        self.setup_styles()

        # --- ROOT GRID ARCHITECTURE ---
        # Column 0: Navigation (Fixed), Column 1: Workspace (Expands)
        self.grid_columnconfigure(1, weight=1) 
        self.grid_rowconfigure(0, weight=1)

        # 2. SIDEBAR (Navigation)
        self.sidebar = tk.Frame(self, bg="#000000", width=200)
        self.sidebar.grid(row=0, column=0, sticky="nsew")
        self.sidebar.grid_propagate(False)

        # 3. WORKSPACE (Dynamic Content)
        self.container = tk.Frame(self, bg=C_BG)
        self.container.grid(row=0, column=1, sticky="nsew")
        self.container.grid_rowconfigure(0, weight=1)
        self.container.grid_columnconfigure(0, weight=1)

        self.setup_nav()
        
        # Initialize Frames
        self.frames = {}
        for F in (DashboardPage, DrivePage, ArmPage, LogPage, SettingsPage):
            page_name = F.__name__
            frame = F(parent=self.container, controller=self)
            self.frames[page_name] = frame
            frame.grid(row=0, column=0, sticky="nsew")

        self.show_frame("DashboardPage")

    def setup_styles(self):
        style = ttk.Style()
        style.theme_use("clam")
        
        # Global Scrollbar
        style.configure("Vertical.TScrollbar", gripcount=0, background="#222", darkcolor="#222", lightcolor="#222", bordercolor="#222", arrowcolor=C_ACCENT)
        
        # Dark Treeview for Logs
        style.configure("Treeview", background=C_PANEL, foreground=C_TEXT, fieldbackground=C_PANEL, rowheight=35, font=F_UI, borderwidth=0)
        style.configure("Treeview.Heading", background="#000", foreground=C_ACCENT, relief="flat", font=F_SUB)
        style.map("Treeview", background=[('selected', C_ACCENT)], foreground=[('selected', 'black')])

    def setup_nav(self):
        tk.Label(self.sidebar, text="ARES V1", font=F_HEAD, fg=C_ACCENT, bg="#000000", pady=40).pack()
        
        nav_btns = [
            ("DASHBOARD", "DashboardPage"), 
            ("DRIVE", "DrivePage"), 
            ("ARM", "ArmPage"), 
            ("LOGS", "LogPage"),
            ("SETTINGS", "SettingsPage")
        ]
        
        for text, page in nav_btns:
            btn = tk.Button(self.sidebar, text=text, font=F_SUB, bg="#000000", fg=C_TEXT,
                            activebackground=C_ACCENT, activeforeground="black",
                            bd=0, pady=20, cursor="hand2", command=lambda p=page: self.show_frame(p))
            btn.pack(fill="x")

    def show_frame(self, page_name):
        self.frames[page_name].tkraise()

# --- DASHBOARD PAGE ---
class DashboardPage(tk.Frame):
    def __init__(self, parent, controller):
        super().__init__(parent, bg=C_BG, padx=15, pady=15)
        
        self.grid_columnconfigure(0, weight=3)
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(0, weight=3)
        self.grid_rowconfigure(1, weight=2)

        # Video Feed
        self.cam_box = tk.Frame(self, bg="black", highlightbackground=C_ACCENT, highlightthickness=1)
        self.cam_box.grid(row=0, column=0, sticky="nsew", padx=5, pady=5)
        tk.Label(self.cam_box, text="PRIMARY OPTICS - LIVE", fg=C_ACCENT, bg="black", font=F_SUB).place(relx=0.5, rely=0.5, anchor="center")

        # Telemetry
        self.sens_box = tk.Frame(self, bg=C_PANEL, padx=20, pady=20)
        self.sens_box.grid(row=0, column=1, sticky="nsew", padx=5, pady=5)
        tk.Label(self.sens_box, text="TELEMETRY", fg=C_ACCENT, bg=C_PANEL, font=F_SUB).pack(anchor="w", pady=(0,20))
        
        self.add_metric(self.sens_box, "AMBIENT TEMP", "24.5°C")
        self.add_metric(self.sens_box, "HUMIDITY", "42%")
        self.add_metric(self.sens_box, "CO2 LEVELS", "405 ppm")

        # Map
        self.map_box = tk.Frame(self, bg=C_PANEL)
        self.map_box.grid(row=1, column=0, columnspan=2, sticky="nsew", padx=5, pady=5)
        self.map = tkintermapview.TkinterMapView(self.map_box, corner_radius=0)
        self.map.pack(fill="both", expand=True)
        self.map.set_position(37.7749, -122.4194)

    def add_metric(self, parent, label, val):
        f = tk.Frame(parent, bg=C_PANEL, pady=10)
        f.pack(fill="x")
        tk.Label(f, text=label, fg="#777", bg=C_PANEL, font=F_UI).pack(anchor="w")
        tk.Label(f, text=val, fg=C_ACCENT, bg=C_PANEL, font=F_DIGIT).pack(anchor="w")

# --- DRIVE PAGE (FIXED RADAR) ---
class DrivePage(tk.Frame):
    def __init__(self, parent, controller):
        super().__init__(parent, bg=C_BG, padx=15, pady=15)
        self.grid_columnconfigure(0, weight=1)
        self.grid_columnconfigure(1, weight=2)
        self.grid_rowconfigure(0, weight=1)

        # Controls
        self.ctrl = tk.Frame(self, bg=C_PANEL, padx=30)
        self.ctrl.grid(row=0, column=0, sticky="nsew", padx=10)
        tk.Label(self.ctrl, text="MANUAL DRIVE", font=F_SUB, fg=C_ACCENT, bg=C_PANEL, pady=40).pack()
        
        self.keys = {}
        kf = tk.Frame(self.ctrl, bg=C_PANEL)
        kf.pack(pady=20)
        for k, r, c in [('w',0,1),('a',1,0),('s',1,1),('d',1,2)]:
            l = tk.Label(kf, text=k.upper(), font=("Arial", 18, "bold"), bg="#222", fg="white", width=4, height=2)
            l.grid(row=r, column=c, padx=5, pady=5)
            self.keys[k] = l

        # Radar Visualization
        self.canvas = tk.Canvas(self, bg="#050505", highlightthickness=0)
        self.canvas.grid(row=0, column=1, sticky="nsew", padx=10)
        
        controller.bind("<KeyPress>", self.on_press)
        controller.bind("<KeyRelease>", self.on_release)
        self.draw_radar()

    def on_press(self, e):
        k = e.char.lower()
        if k in self.keys: self.keys[k].config(bg=C_ACCENT, fg="black")
    def on_release(self, e):
        k = e.char.lower()
        if k in self.keys: self.keys[k].config(bg="#222", fg="white")

    def draw_radar(self):
        self.canvas.delete("all")
        self.update()
        w, h = self.canvas.winfo_width(), self.canvas.winfo_height()
        cx, cy = w//2, h//2
        
        rw, rh = 90, 140
        self.canvas.create_rectangle(cx-rw//2, cy-rh//2, cx+rw//2, cy+rh//2, outline=C_ACCENT, width=2)
        
        # Radiating Outward Logic
        sensors = [
            (cx, cy - rh//2 - 10, 60),   # Front
            (cx, cy + rh//2 + 10, 240),  # Back
            (cx - rw//2 - 10, cy, 150),  # Left
            (cx + rw//2 + 10, cy, 330)   # Right
        ]
        for ax, ay, ang in sensors:
            d = random.randint(30, 120)
            col = C_DANGER if d < 50 else C_ACCENT
            for i in range(3):
                r = d + (i * 15)
                self.canvas.create_arc(ax-r, ay-r, ax+r, ay+r, start=ang, extent=60, outline=col, style="arc", width=2)
        
        self.after(200, self.draw_radar)

# --- ARM PAGE (FIXED KINEMATICS) ---
class ArmPage(tk.Frame):
    def __init__(self, parent, controller):
        super().__init__(parent, bg=C_BG, padx=15, pady=15)
        self.grid_columnconfigure(0, weight=1)
        self.grid_columnconfigure(1, weight=2)
        self.grid_rowconfigure(0, weight=1)

        self.s_panel = tk.Frame(self, bg=C_PANEL, padx=20)
        self.s_panel.grid(row=0, column=0, sticky="nsew", padx=10)
        tk.Label(self.s_panel, text="6-AXIS TELEMETRY", font=F_SUB, fg=C_ACCENT, bg=C_PANEL, pady=30).pack()
        
        self.sliders = []
        for j in ["BASE ROT", "SHOULDER", "ELBOW", "WRIST P", "WRIST R", "CLAW"]:
            f = tk.Frame(self.s_panel, bg=C_PANEL, pady=8)
            f.pack(fill="x")
            tk.Label(f, text=j, fg="#777", bg=C_PANEL, width=12, font=F_UI).pack(side="left")
            s = ttk.Scale(f, from_=0, to=180, orient="horizontal")
            s.set(90)
            s.pack(side="left", fill="x", expand=True, padx=10)
            self.sliders.append(s)

        self.canvas = tk.Canvas(self, bg="#050505", highlightthickness=0)
        self.canvas.grid(row=0, column=1, sticky="nsew", padx=10)
        self.animate_arm()

    def animate_arm(self):
        self.canvas.delete("all")
        self.update()
        w, h = self.canvas.winfo_width(), self.canvas.winfo_height()
        ox, oy = w//2, h-100
        
        # Chained Math
        a1 = math.radians(float(self.sliders[1].get())-180)
        a2 = math.radians(float(self.sliders[2].get())-180)
        a3 = math.radians(float(self.sliders[3].get())-180)
        
        l1, l2, l3 = 140, 110, 60
        x1, y1 = ox + l1*math.cos(a1), oy + l1*math.sin(a1)
        x2, y2 = x1 + l2*math.cos(a1+a2), y1 + l2*math.sin(a1+a2)
        x3, y3 = x2 + l3*math.cos(a1+a2+a3), y2 + l3*math.sin(a1+a2+a3)

        self.canvas.create_rectangle(ox-50, oy, ox+50, oy+20, fill="#222")
        self.canvas.create_line(ox, oy, x1, y1, fill=C_ACCENT, width=12, capstyle="round")
        self.canvas.create_line(x1, y1, x2, y2, fill="white", width=8, capstyle="round")
        self.canvas.create_line(x2, y2, x3, y3, fill="#888", width=5, capstyle="round")
        
        for px, py in [(ox, oy), (x1, y1), (x2, y2)]:
            self.canvas.create_oval(px-6, py-6, px+6, py+6, fill=C_BG, outline=C_ACCENT)
        
        self.after(50, self.animate_arm)

# --- LOG PAGE (DARK MODE DETAILED) ---
class LogPage(tk.Frame):
    def __init__(self, parent, controller):
        super().__init__(parent, bg=C_BG, padx=20, pady=20)
        self.controller = controller
        tk.Label(self, text="MISSION TELEMETRY ARCHIVE", font=F_HEAD, fg=C_ACCENT, bg=C_BG).pack(anchor="w", pady=(0,15))
        
        tree_frame = tk.Frame(self, bg=C_BG)
        tree_frame.pack(fill="both", expand=True)

        cols = ("TIME", "LVL", "SRC", "EVENT", "DATA")
        self.tree = ttk.Treeview(tree_frame, columns=cols, show='headings')
        for c in cols: self.tree.heading(c, text=c)
        
        self.tree.column("TIME", width=100, anchor="center")
        self.tree.column("LVL", width=80, anchor="center")
        self.tree.column("SRC", width=120, anchor="center")
        self.tree.column("EVENT", width=450, anchor="w")
        self.tree.column("DATA", width=150, anchor="e")

        scrolly = ttk.Scrollbar(tree_frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrolly.set)
        
        self.tree.pack(side="left", fill="both", expand=True)
        scrolly.pack(side="right", fill="y")

        self.tree.tag_configure('WARN', foreground=C_ACCENT)
        self.tree.tag_configure('CRIT', foreground="white", background="#451a1a")

        for log in self.controller.config["mission_logs"]:
            self.tree.insert("", "end", values=(log["time"], log["lvl"], log["src"], log["event"], log["data"]), tags=(log["lvl"],))

# --- SETTINGS PAGE ---
class SettingsPage(tk.Frame):
    def __init__(self, parent, controller):
        super().__init__(parent, bg=C_BG, padx=40, pady=40)
        self.grid_columnconfigure((0,1), weight=1)

        net = tk.LabelFrame(self, text=" NETWORK ", font=F_SUB, fg=C_ACCENT, bg=C_BG, padx=20, pady=20)
        net.grid(row=0, column=0, sticky="nsew", padx=10)
        self.add_field(net, "CAM IP", "192.168.1.50")
        self.add_field(net, "WS URL", "ws://192.168.1.100:81")

        safe = tk.LabelFrame(self, text=" PROTOCOLS ", font=F_SUB, fg=C_ACCENT, bg=C_BG, padx=20, pady=20)
        safe.grid(row=0, column=1, sticky="nsew", padx=10)
        tk.Checkbutton(safe, text="LOG TELEMETRY", variable=controller.config["logging"], bg=C_BG, fg=C_STATUS, selectcolor="#000").pack(pady=10)

    def add_field(self, p, l, v):
        f = tk.Frame(p, bg=C_BG, pady=10)
        f.pack(fill="x")
        tk.Label(f, text=l, fg="#888", bg=C_BG, width=12).pack(side="left")
        e = tk.Entry(f, bg="#222", fg="white", bd=0)
        e.insert(0, v)
        e.pack(side="right", fill="x", expand=True, padx=10, ipady=6)

if __name__ == "__main__":
    app = ARES_CommandCenter()
    app.mainloop()