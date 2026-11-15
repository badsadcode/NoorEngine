#include "noor/foundation/file_io.h"
#include "noor/foundation/guid.h"
#include "noor/foundation/sha256.h"

#include <filesystem>
#include <ostream>
#include <doctest/doctest.h>

namespace stdfs = std::filesystem;

TEST_CASE("sha256: FIPS 180-4 test vectors") {
    CHECK(noor::Sha256::hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(noor::Sha256::hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(noor::Sha256::hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(noor::Sha256::hex(std::string(1'000'000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256: streaming in pieces equals one shot") {
    noor::Sha256 s;
    s.update("abcdbcdecdefdefgefgh");
    s.update("fghighijhijkijkljklmklmnlmnomnopnopq");
    const auto d = s.finish();
    CHECK(d[0] == 0x24);
    CHECK(d[31] == 0xc1);
}

TEST_CASE("file io: atomic write replaces content and keeps one backup") {
    const stdfs::path dir = stdfs::temp_directory_path() / ("noor_fileio_" + noor::Guid::generate().to_string());
    const stdfs::path file = dir / "sub" / "doc.txt";
    REQUIRE(noor::fs::write_file_atomic(file, "first"));
    REQUIRE(noor::fs::write_file_atomic(file, "second"));
    CHECK(*noor::fs::read_file(file) == "second");
    CHECK(*noor::fs::read_file(stdfs::path(file) += ".bak") == "first");
    CHECK_FALSE(stdfs::exists(stdfs::path(file) += ".tmp"));
    stdfs::remove_all(dir);
}

TEST_CASE("file io: missing files and size limits are reported, not thrown") {
    const auto missing = noor::fs::read_file("definitely/not/here.txt");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == noor::ErrorCode::NotFound);

    const stdfs::path dir = stdfs::temp_directory_path() / ("noor_fileio_" + noor::Guid::generate().to_string());
    REQUIRE(noor::fs::write_file_atomic(dir / "big.txt", std::string(100, 'x'), false));
    const auto limited = noor::fs::read_file(dir / "big.txt", 10);
    REQUIRE_FALSE(limited);
    CHECK(limited.error().code == noor::ErrorCode::InvalidArgument);
    stdfs::remove_all(dir);
}
