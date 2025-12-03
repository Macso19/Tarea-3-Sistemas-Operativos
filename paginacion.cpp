#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>
#include <cstdlib>
#include <iomanip>

using namespace std;

#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define BOLD    "\033[1m"

struct Frame {
    bool is_free = true;
    int pid_owner = -1;
    int virtual_page = -1;
    int timestamp = 0;
};

struct Process {
    int pid;
    int size_mb;
    int num_pages;
    vector<int> page_table;
    bool alive = true;
};

vector<Frame> RAM;
vector<Frame> SWAP;
vector<Process> procesos;

int ram_size_mb;
int virt_size_mb;
int swap_size_mb;
int page_size_kb;

int total_ram_frames;
int total_virtual_frames;
int total_swap_frames;

atomic<int> global_time_sec(0);
atomic<bool> running(true);

mutex mtx;
mt19937 rng(random_device{}());

void mostrar_estado_visual() {
    int ram_used = 0;
    for (const auto &f : RAM) if (!f.is_free) ram_used++;

    int swap_used = 0;
    for (const auto &f : SWAP) if (!f.is_free) swap_used++;

    double ram_pct = (double)ram_used / total_ram_frames * 100.0;
    double swap_pct = (double)swap_used / total_swap_frames * 100.0;

    cout << "\n" << BOLD << " ESTADO DE MEMORIA (T=" << global_time_sec << "s) " << RESET << "\n";
    
    cout << "RAM  [" << GREEN;
    int bar_width = 30;
    int pos = bar_width * ram_pct / 100;
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) cout << "|";
        else cout << ".";
    }
    cout << RESET << "] " << ram_used << "/" << total_ram_frames << " frames (" << (int)ram_pct << "%)\n";

    cout << "SWAP [" << YELLOW;
    pos = bar_width * swap_pct / 100;
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) cout << "|";
        else cout << ".";
    }
    cout << RESET << "] " << swap_used << "/" << total_swap_frames << " frames (" << (int)swap_pct << "%)\n";
    cout << string(50, '-') << "\n";
}

int elegir_proceso_vivo() {
    vector<int> vivos;
    for (int i = 0; i < (int)procesos.size(); ++i)
        if (procesos[i].alive) vivos.push_back(i);
    if (vivos.empty()) return -1;
    uniform_int_distribution<int> dist(0, (int)vivos.size() - 1);
    return vivos[dist(rng)];
}

int elegir_victima_lru() {
    int idx = -1;
    int oldest_ts = INT32_MAX;
    for (int i = 0; i < (int)RAM.size(); ++i) {
        if (!RAM[i].is_free && RAM[i].timestamp < oldest_ts) {
            oldest_ts = RAM[i].timestamp;
            idx = i;
        }
    }
    return idx;
}

int frame_libre_swap() {
    for (int i = 0; i < (int)SWAP.size(); ++i)
        if (SWAP[i].is_free) return i;
    return -1;
}

void manejar_page_fault(int pid, int page) {
    Process &p = procesos[pid];
    int loc = p.page_table[page];
    int idx_swap = -loc - 1;

    cout << YELLOW << "    [!] PAGE FAULT: Proceso " << pid 
         << " pide pág " << page << " (estaba en SWAP fr " << idx_swap << ")" << RESET << "\n";

    int idx_ram_libre = -1;
    for (int i = 0; i < (int)RAM.size(); ++i) {
        if (RAM[i].is_free) { idx_ram_libre = i; break; }
    }

    if (idx_ram_libre == -1) {
        int victima = elegir_victima_lru();
        if (victima == -1) {
            cout << RED << "     CRITICO!!!: No hay frame disponible en RAM ni víctimas. Fin.\n" << RESET;
            running = false;
            return;
        }

        Frame &f_v = RAM[victima];
        int idx_swap_libre = frame_libre_swap();
        
        if (idx_swap_libre == -1) {
            cout << RED << "     CRITICO!!!: SWAP LLENA. No se puede hacer swap-out. Fin.\n" << RESET;
            running = false;
            return;
        }

        SWAP[idx_swap_libre].is_free = false;
        SWAP[idx_swap_libre].pid_owner = f_v.pid_owner;
        SWAP[idx_swap_libre].virtual_page = f_v.virtual_page;

        Process &pv = procesos[f_v.pid_owner];
        pv.page_table[f_v.virtual_page] = -(idx_swap_libre + 1);

        cout << MAGENTA << "    ↪ REEMPLAZO LRU: Pág (" << f_v.pid_owner << ", " << f_v.virtual_page 
             << ") movida RAM[" << victima << "] -> SWAP[" << idx_swap_libre << "]" << RESET << "\n";

        idx_ram_libre = victima;
    }

    Frame &f_ram = RAM[idx_ram_libre];
    Frame &f_swap = SWAP[idx_swap];

    f_ram.is_free = false;
    f_ram.pid_owner = pid;
    f_ram.virtual_page = page;
    f_ram.timestamp = global_time_sec.load();

    f_swap.is_free = true;
    f_swap.pid_owner = -1;
    f_swap.virtual_page = -1;

    p.page_table[page] = idx_ram_libre;

    cout << GREEN << "    ✔ RESTAURADO: Pág (" << pid << ", " << page 
         << ") cargada en RAM[" << idx_ram_libre << "]" << RESET << "\n";
}

int crear_proceso(int size_mb) {
    lock_guard<mutex> lk(mtx);

    Process p;
    p.pid = (int)procesos.size();
    p.size_mb = size_mb;
    p.num_pages = (size_mb * 1024 + page_size_kb - 1) / page_size_kb;
    p.alive = true;
    p.page_table.assign(p.num_pages, -9999);

    cout << CYAN << " CREANDO Proceso " << p.pid << " (" << size_mb << " MB, " 
         << p.num_pages << " págs)..." << RESET << "\n";

    int frames_needed = p.num_pages;
    int frames_assigned = 0;

    for (int i = 0; i < (int)RAM.size() && frames_assigned < frames_needed; ++i) {
        if (RAM[i].is_free) {
            RAM[i].is_free = false;
            RAM[i].pid_owner = p.pid;
            RAM[i].virtual_page = frames_assigned;
            RAM[i].timestamp = global_time_sec.load();
            p.page_table[frames_assigned] = i;
            frames_assigned++;
        }
    }

    for (int i = 0; i < (int)SWAP.size() && frames_assigned < frames_needed; ++i) {
        if (SWAP[i].is_free) {
            SWAP[i].is_free = false;
            SWAP[i].pid_owner = p.pid;
            SWAP[i].virtual_page = frames_assigned;
            p.page_table[frames_assigned] = -(i + 1);
            frames_assigned++;
        }
    }

    if (frames_assigned < frames_needed) {
        cout << RED << " ERROR: Memoria insuficiente (RAM+SWAP) para proceso nuevo. Fin simulación.\n" << RESET;
        running = false;
        return -1;
    }

    procesos.push_back(std::move(p));
    mostrar_estado_visual();
    return procesos.back().pid;
}

void matar_proceso_aleatorio() {
    lock_guard<mutex> lk(mtx);

    int idx = elegir_proceso_vivo();
    if (idx == -1) return;

    Process &p = procesos[idx];
    p.alive = false;

    cout << RED << "☠ KILLER: Eliminando proceso " << p.pid << " y liberando memoria." << RESET << "\n";

    for (int page = 0; page < p.num_pages; ++page) {
        int loc = p.page_table[page];
        if (loc >= 0) {
            RAM[loc].is_free = true;
            RAM[loc].pid_owner = -1;
            RAM[loc].virtual_page = -1;
        } else if (loc != -9999) {
            int idx_s = -loc - 1;
            SWAP[idx_s].is_free = true;
            SWAP[idx_s].pid_owner = -1;
            SWAP[idx_s].virtual_page = -1;
        }
    }
    mostrar_estado_visual();
}

void acceso_memoria_aleatorio() {
    lock_guard<mutex> lk(mtx);

    int idx = elegir_proceso_vivo();
    if (idx == -1) return;

    Process &p = procesos[idx];
    if (p.num_pages == 0) return;

    uniform_int_distribution<int> dist_page(0, p.num_pages - 1);
    int page = dist_page(rng);

    cout << BLUE << " ACCESO: Proceso " << p.pid << " busca pág " << page << " -> " << RESET;

    int loc = p.page_table[page];

    if (loc >= 0) {
        cout << GREEN << "En RAM (frame " << loc << ")" << RESET << "\n";
        RAM[loc].timestamp = global_time_sec.load();
    } else {
        cout << YELLOW << "PAGE FAULT" << RESET << "\n";
        manejar_page_fault(p.pid, page);
        mostrar_estado_visual();
    }
}

void hilo_tiempo() {
    while (running) {
        this_thread::sleep_for(chrono::seconds(1));
        global_time_sec++;
    }
}

void hilo_creador_procesos(int min_mb, int max_mb) {
    uniform_int_distribution<int> dist_size(min_mb, max_mb);
    while (running) {
        this_thread::sleep_for(chrono::seconds(2)); 
        if (!running) break;
        int size = dist_size(rng);
        if (crear_proceso(size) == -1) break;
    }
}

void hilo_killer() {
    while (running) {
        this_thread::sleep_for(chrono::seconds(5));
        if (!running) break;
        if (global_time_sec.load() < 30) continue;
        matar_proceso_aleatorio();
    }
}

void hilo_acceso_memoria() {
    while (running) {
        this_thread::sleep_for(chrono::seconds(5));
        if (!running) break;
        if (global_time_sec.load() < 30) continue;
        acceso_memoria_aleatorio();
    }
}

int main() {
    cout << BOLD << " Simulador de Paginacion / Algoritmo de reemplazo LRU" << RESET << "\n";

    cout << "Tamaño RAM (MB): "; cin >> ram_size_mb;
    cout << "Tamaño de página (KB): "; cin >> page_size_kb;

    int min_proc_mb, max_proc_mb;
    cout << "Tamaño mínimo de proceso (MB): "; cin >> min_proc_mb;
    cout << "Tamaño máximo de proceso (MB): "; cin >> max_proc_mb;

    uniform_real_distribution<double> dist_factor(1.5, 4.5);
    double factor = dist_factor(rng);
    virt_size_mb = static_cast<int>(ram_size_mb * factor);
    if (virt_size_mb <= ram_size_mb) virt_size_mb = ram_size_mb * 2; 

    total_ram_frames = (ram_size_mb * 1024) / page_size_kb;
    total_virtual_frames = (virt_size_mb * 1024) / page_size_kb;
    total_swap_frames = total_virtual_frames - total_ram_frames;

    if (total_swap_frames <= 0) {
        cout << RED << "Configuración inválida (SWAP <= 0)." << RESET << "\n";
        return 1;
    }

    swap_size_mb = virt_size_mb - ram_size_mb;

    cout << "\n" << BOLD << "--- CONFIGURACIÓN INICIAL ---" << RESET << "\n";
    cout << "RAM:  " << ram_size_mb << " MB (" << total_ram_frames << " frames)\n";
    cout << "VIRT: " << virt_size_mb << " MB (" << total_virtual_frames << " frames)\n";
    cout << "SWAP: " << swap_size_mb << " MB (" << total_swap_frames << " frames)\n";
    cout << "-----------------------------\n\n";

    RAM.assign(total_ram_frames, Frame{});
    SWAP.assign(total_swap_frames, Frame{});

    thread t_time(hilo_tiempo);
    thread t_creator(hilo_creador_procesos, min_proc_mb, max_proc_mb);
    thread t_killer(hilo_killer);
    thread t_access(hilo_acceso_memoria);

    t_creator.join();
    
    running = false;
    t_time.join();
    t_killer.join();
    t_access.join();

    cout << "\n" << RED << BOLD << "Simulación terminada." << RESET << "\n";
    return 0;
}