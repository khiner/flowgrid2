#include <thread>
#include "config.h"
#include "context.h"
#include "draw.h"
#include "process_manager.h"

Config config{};
Context context{}; // NOLINT(cert-err58-cpp)
Context &c = context; // Convenient shorthand
const State &state = c.s;
const State &s = c.s; // Convenient shorthand
State &ui_s = c.ui_s; // Convenient shorthand
BlockingConcurrentQueue<Action> q{}; // NOLINT(cert-err58-cpp,cppcoreguidelines-interfaces-global-init)

int main(int, const char *argv[]) {
    config.faust_libraries_path = std::string(argv[0]) + "/../../lib/faust/libraries";

    ProcessManager pm;
    std::thread action_consumer([&]() {
        while (s.action_consumer.running) {
            Action a;
            q.wait_dequeue(a);
            c.on_action(a);
            pm.on_action(a);
        }
    });

    draw();
    action_consumer.join();
    return 0;
}
