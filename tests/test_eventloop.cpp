#include <catch2/catch_test_macros.hpp>
#include "Manager.h"
#include <unistd.h>
#include <fcntl.h>

TEST_CASE("FdGuard default constructed has fd -1", "[eventloop][raii]")
{
    FdGuard g;
    REQUIRE(g.get() == -1);
}

TEST_CASE("FdGuard closes fd on destruction", "[eventloop][raii]")
{
    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);
    int read_end = pipefd[0];
    int write_end = pipefd[1];

    {
        FdGuard g(write_end);
        REQUIRE(g.get() == write_end);
        // g goes out of scope, should close write_end
    }

    // Writing to read_end's peer (write_end) should fail now
    // because write_end was closed by FdGuard destructor.
    // The read_end should get EPIPE when we try to write to it.
    // Alternatively, verify by checking that write_end fd is invalid.
    // Best: use fcntl to check if write_end is still valid.
    // After close, fcntl should return -1 with EBADF.
    REQUIRE(fcntl(write_end, F_GETFL) == -1);
    REQUIRE(errno == EBADF);

    // Clean up read_end
    close(read_end);
}

TEST_CASE("FdGuard move transfers ownership", "[eventloop][raii]")
{
    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);

    FdGuard a(pipefd[1]);
    REQUIRE(a.get() == pipefd[1]);

    FdGuard b(std::move(a));
    REQUIRE(b.get() == pipefd[1]);
    REQUIRE(a.get() == -1);  // moved-from

    close(pipefd[0]);
    // b will close pipefd[1] on destruction
}

TEST_CASE("FdGuard move assignment transfers ownership", "[eventloop][raii]")
{
    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);

    FdGuard a(pipefd[1]);
    REQUIRE(a.get() == pipefd[1]);

    FdGuard b;
    b = std::move(a);
    REQUIRE(b.get() == pipefd[1]);
    REQUIRE(a.get() == -1);  // moved-from

    close(pipefd[0]);
    // b will close pipefd[1] on destruction
}

TEST_CASE("Self-pipe can be created and used", "[eventloop][pipe]")
{
    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);

    // Write a byte to the write end
    char c = 'x';
    REQUIRE(write(pipefd[1], &c, 1) == 1);

    // Read it back from the read end
    char buf;
    REQUIRE(read(pipefd[0], &buf, 1) == 1);
    REQUIRE(buf == 'x');

    close(pipefd[0]);
    close(pipefd[1]);
}
