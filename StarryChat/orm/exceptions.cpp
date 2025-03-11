#include <mysql/mysql.h>
#include "exceptions.h"

namespace StarryChat::orm {

std::unique_ptr<DatabaseException> createExceptionFromMySqlError(
    int errorCode,
    const std::string& errorMessage,
    const std::string& sql) {
  switch (errorCode) {

  }
}

}  // namespace StarryChat::orm
