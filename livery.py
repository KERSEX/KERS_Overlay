import socket
import struct
import tkinter as tk
import colorsys

# === UDP EINSTELLUNGEN ===
UDP_IP = "0.0.0.0"
UDP_PORT = 20777

# === DEINE F1 WORLD FARBEN (Farbton, Sättigung, Luminanz) ===
# Werte basierend auf der In-Game Skala (0-239)
MY_F1W_COLORS_HSL = [
    [0, 0, 130],
    [30, 200, 130],
    [70, 140, 70],
    [30, 240, 15]
]

# Format-Strings für das Entpacken (Little Endian)
HEADER_FORMAT = "<H 5B Q f 2I 2B"
HEADER_SIZE = 29
PARTICIPANT_FORMAT = "<B 3H 3B 32s 2B H 2B 12B"
PARTICIPANT_SIZE = 60

class F1TelemetryApp:
    def __init__(self, root):
        self.root = root
        self.root.title("F1 26 F1W Livery Viewer")
        self.root.geometry("450x180")
        self.root.configure(bg="#2b2b2b")
        
        # UDP Socket einrichten (Non-Blocking)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((UDP_IP, UDP_PORT))
        self.sock.setblocking(False)

        # Fahrer-Name Label
        self.name_label = tk.Label(self.root, text="Warte auf Telemetrie-Daten...", font=("Arial", 12, "bold"), bg="#2b2b2b", fg="white")
        self.name_label.pack(pady=10)

        # Rahmen für die Farb-Boxen
        self.colors_container = tk.Frame(self.root, bg="#2b2b2b")
        self.colors_container.pack()

        # GUI Elemente für die 4 Farben
        self.color_frames = []
        self.color_labels = []
        
        # Hex-Werte aus deinen HSL-Daten generieren
        self.hex_colors = [self.hsl_to_hex(h, s, l) for h, s, l in MY_F1W_COLORS_HSL]
        
        for i in range(4):
            frame = tk.Frame(self.colors_container, width=90, height=90, bg="gray")
            frame.pack(side=tk.LEFT, padx=10)
            frame.pack_propagate(False) # Verhindert, dass der Frame schrumpft
            
            label = tk.Label(frame, text=f"Color {i+1}\nWaiting...", bg="gray", fg="white")
            label.place(relx=0.5, rely=0.5, anchor=tk.CENTER)
            
            self.color_frames.append(frame)
            self.color_labels.append(label)

        # Update-Loop starten
        self.update_telemetry()

    def hsl_to_hex(self, h_raw, s_raw, l_raw):
        """Rechnet die F1 0-239 Werte in echte Hex-Codes um"""
        h = min(h_raw / 239.0, 1.0)
        s = min(s_raw / 239.0, 1.0)
        l = min(l_raw / 239.0, 1.0)
        
        r_float, g_float, b_float = colorsys.hls_to_rgb(h, l, s)
        
        r = int(r_float * 255)
        g = int(g_float * 255)
        b = int(b_float * 255)
        
        return f'#{r:02x}{g:02x}{b:02x}'

    def hex_to_rgb(self, hex_color):
        """Hilfsfunktion für den Textkontrast"""
        hex_color = hex_color.lstrip('#')
        return tuple(int(hex_color[i:i+2], 16) for i in (0, 2, 4))

    def update_telemetry(self):
        try:
            while True:
                # Daten vom Socket lesen
                data, _ = self.sock.recvfrom(2048)
                
                if len(data) < HEADER_SIZE:
                    continue
                
                # Header entpacken
                header = struct.unpack(HEADER_FORMAT, data[:HEADER_SIZE])
                packet_id = header[5]
                player_car_index = header[10]
                
                # Wir interessieren uns nur für Packet ID 4 (Participants)
                if packet_id == 4:
                    offset = HEADER_SIZE + 1
                    
                    # Zum Datenblock des Spielers springen
                    player_offset = offset + (player_car_index * PARTICIPANT_SIZE)
                    player_data = data[player_offset : player_offset + PARTICIPANT_SIZE]
                    
                    if len(player_data) == PARTICIPANT_SIZE:
                        participant = struct.unpack(PARTICIPANT_FORMAT, player_data)
                        
                        # Fahrername auslesen (ist das 7. Element, 32 Bytes char array)
                        name_bytes = participant[7]
                        driver_name = name_bytes.decode('utf-8', errors='ignore').split('\x00')[0]
                        
                        self.name_label.config(text=f"Fahrer: {driver_name}")
                        
                        # Die fest definierten Farben auf die GUI anwenden
                        for i in range(4):
                            hex_color = self.hex_colors[i]
                            r, g, b = self.hex_to_rgb(hex_color)
                            
                            self.color_frames[i].config(bg=hex_color)
                            self.color_labels[i].config(
                                bg=hex_color, 
                                text=f"Color {i+1}\n{hex_color}",
                                # Helligkeits-Check für den Textkontrast (weiß oder schwarz)
                                fg="black" if (r*0.299 + g*0.587 + b*0.114) > 186 else "white"
                            )
                            
        except BlockingIOError:
            pass
        except Exception as e:
            print(f"Fehler beim Verarbeiten: {e}")

        # Ruft sich selbst nach 50ms wieder auf
        self.root.after(50, self.update_telemetry)

if __name__ == "__main__":
    root = tk.Tk()
    app = F1TelemetryApp(root)
    root.mainloop()