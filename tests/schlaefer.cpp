// Hilfsprogramm fuer tst_local: spielt den Server der Python-Fassung (data\main.exe),
// der nach dem Update von 0.2.6 weiterlaeuft.
//
//   (ohne)               wartet, bis es beendet wird
//   --port N             haelt dabei 127.0.0.1:N (TCP) besetzt - wie main.exe die 5100
//   --udp N              haelt UDP N auf allen Adressen besetzt - wie main.exe die 20777
//   --bootloader ...     startet sich selbst noch einmal (mit den restlichen Argumenten)
//                        und wartet auf das Kind, Rueckgabe 0 - wie eine
//                        PyInstaller-onefile-EXE: zwei Prozesse mit demselben Pfad
#include <winsock2.h>
#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
    int i = 1;
    if (i < argc && std::strcmp(argv[i], "--bootloader") == 0) {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(self) + L"\"";
        for (++i; i < argc; ++i) {
            cmd += L' ';
            for (const char* c = argv[i]; *c; ++c)
                cmd += wchar_t(*c);
        }
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(self, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
            return 2;
        WaitForSingleObject(pi.hProcess, INFINITE);
        return 0;
    }
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    for (; i + 1 < argc; i += 2) {
        const bool tcp = std::strcmp(argv[i], "--port") == 0;
        if (!tcp && std::strcmp(argv[i], "--udp") != 0)
            return 4;
        SOCKET s = tcp ? socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) : socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(u_short(std::atoi(argv[i + 1])));
        a.sin_addr.s_addr = htonl(tcp ? INADDR_LOOPBACK : INADDR_ANY);   // wie main.py: UDP auf allen
        if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || (tcp && listen(s, 4) != 0))
            return 3;
    }
    Sleep(INFINITE);
    return 0;
}
