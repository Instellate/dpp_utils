#include <dpp/cluster.h>
#include <dpp_utils/command_controller.h>
#include <dpp_utils/database.h>
#include <dpp_utils/database_exception.h>

void test_cmd(const dpp::slashcommand_t &event, const std::string &name) {}

int main() {
    dpp::cluster cluster;

    auto db = std::make_shared<dpp_utils::database>(
        "postgresql://instellate:instellate@localhost:5432/dpp_utils");
    db->start(cluster);

    auto task = [db]() -> dpp::task<void> {
        dpp_utils::result result = co_await db->co_query(
            "SELECT * FROM users WHERE id = $1", 565197576026980365);
        dpp_utils::row row = result[0];
        std::cout << "{ id: " << row.get<long>("id") << ", name: \""
                  << row.get<std::string>("name") << "\" }\n";
        co_return;
    }();

    while (true) {
        cluster.socketengine->process_events();
    };

    return 0;
}