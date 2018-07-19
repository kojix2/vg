/// \file blocked_gzip_input_stream.cpp
///  
/// Unit tests for BlockedGzipInputStream

#include "../blocked_gzip_input_stream.hpp"
#include "../blocked_gzip_output_stream.hpp"
#include "../hfile_cppstream.hpp"
#include "catch.hpp"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/gzip_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>


#include <htslib/hfile.h>

#include <sstream>

namespace vg {
namespace unittest {
using namespace std;
using namespace vg::stream;

// We have a tiny function to get virtual offsets, based on the block's start
// offset in the file, and the offset in the block
static int64_t vo(size_t block_start, size_t offset) {
    return (block_start << 16) | (0xFFFF & offset);
}

TEST_CASE("a BlockedGzipInputStream can read from a stringstream", "[bgzip]") {
    stringstream datastream;
    
    const string TO_COMPRESS = "It's cheap and it's ethical... well, it's ethical... well, it's magical really";
    
    {
        // Write some data in
        BlockedGzipOutputStream bgzip_out(datastream);
        ::google::protobuf::io::CodedOutputStream coded_out(&bgzip_out);
        coded_out.WriteString(TO_COMPRESS);
    }
    
    // Stringstreams track put and get positions separately. So we will read from the beginning.
    REQUIRE(datastream.tellg() == 0);
    
    // Now try and read it back
    BlockedGzipInputStream bgzip_in(datastream);
    
    SECTION("data can be read the first time through, from the start") {
    
        REQUIRE(bgzip_in.Tell() == vo(0, 0));
        
        const char* buffer;
        int buffer_size;
        REQUIRE(bgzip_in.Next((const void**)&buffer, &buffer_size));
        
        int block = 0;
        int good_through = 0;
        
        do {
        
            // Check each block we read out of the stream
            
            // We know that the stream ought to put us at the end of whatever it read.
            // We also know it ought to read one block per Next if we don't back up.
            REQUIRE(bgzip_in.Tell() == vo(block, buffer_size));
            
            for (size_t i = 0; i < buffer_size; i++) {
                // Check all the characters
                REQUIRE(buffer[i] == TO_COMPRESS[good_through + i]);
            }
            
            good_through += buffer_size;
            block++;
            
        } while (bgzip_in.Next((const void**)&buffer, &buffer_size));
        
        REQUIRE(good_through == TO_COMPRESS.size());
    }
    
    SECTION("data can be seeked into") {
    
        // Make sure we started at the start
        REQUIRE(bgzip_in.Tell() == vo(0, 0));
        
        // Go somewhere else and make sure we get there
        REQUIRE(bgzip_in.Seek(vo(0, 10)));
        REQUIRE(bgzip_in.Tell() == vo(0, 10));
        
        const char* buffer;
        int buffer_size = 0;
        while(buffer_size == 0) {
            // Fish for data until we get some
            REQUIRE(bgzip_in.Next((const void**)&buffer, &buffer_size));
        }
        
        // Make sure we got the right data
        REQUIRE(buffer[0] == TO_COMPRESS[10]);
        
        SECTION("seek can seek back") {
        
            REQUIRE(bgzip_in.Seek(vo(0, 0)));
            REQUIRE(bgzip_in.Tell() == vo(0, 0));
            
            buffer_size = 0;
            while(buffer_size == 0) {
                // Fish for data until we get some
                REQUIRE(bgzip_in.Next((const void**)&buffer, &buffer_size));
            }
            
            // Make sure we got the right data
            REQUIRE(buffer[0] == TO_COMPRESS[0]);
            
        }
        
        SECTION("skip works") {
            REQUIRE(bgzip_in.Seek(vo(0, 0)));
            bgzip_in.Skip(5);
            REQUIRE(bgzip_in.Tell() == vo(0, 5));
            
            buffer_size = 0;
            while(buffer_size == 0) {
                // Fish for data until we get some
                REQUIRE(bgzip_in.Next((const void**)&buffer, &buffer_size));
            }
            
            REQUIRE(buffer[0] == TO_COMPRESS[5]);
        }
        
        
    }
        
}

TEST_CASE("a BlockedGzipInputStream can read non-blocked gzip-compressed data", "[bgzip]") {
    stringstream datastream;
    
    const string TO_COMPRESS = "But wait, it comes with a warranty for a week, and that's respectable";
    
    {
        // Write some data in
        ::google::protobuf::io::OstreamOutputStream raw_out(&datastream);
        ::google::protobuf::io::GzipOutputStream gzip_out(&raw_out);
        ::google::protobuf::io::CodedOutputStream coded_out(&gzip_out);
        coded_out.WriteString(TO_COMPRESS);
    }
    
    // Stringstreams track put and get positions separately. So we will read from the beginning.
    REQUIRE(datastream.tellg() == 0);
    
    // Now try and read it back
    BlockedGzipInputStream bgzip_in(datastream);
    
    SECTION("data can be read the first time through, from the start") {
    
        // We can't seek
        REQUIRE(bgzip_in.Tell() == -1);
        
        const char* buffer;
        int buffer_size;
        REQUIRE(bgzip_in.Next((const void**)&buffer, &buffer_size));
        
        int block = 0;
        int good_through = 0;
        
        do {
        
            // Check each block we read out of the stream
            
            // We still can't seek
            REQUIRE(bgzip_in.Tell() == -1);
            
            for (size_t i = 0; i < buffer_size; i++) {
                // Check all the characters
                REQUIRE(buffer[i] == TO_COMPRESS[good_through + i]);
            }
            
            good_through += buffer_size;
            block++;
            
        } while (bgzip_in.Next((const void**)&buffer, &buffer_size));
        
        REQUIRE(good_through == TO_COMPRESS.size());
    }
    
    SECTION("seeking refuses to work") {
        REQUIRE(bgzip_in.Tell() == -1);
        REQUIRE(!bgzip_in.Seek(vo(0, 10)));
        REQUIRE(bgzip_in.Tell() == -1);
        
        const char* buffer;
        int buffer_size = 0;
        while(buffer_size == 0) {
            // Fish for data until we get some
            REQUIRE(bgzip_in.Next((const void**)&buffer, &buffer_size));
        }
        
        // Make sure we got the right data, even though we tried to seek
        REQUIRE(buffer[0] == TO_COMPRESS[0]);
    }
    
    SECTION("Skip works") {
        REQUIRE(bgzip_in.Tell() == -1);
        REQUIRE(bgzip_in.Skip(10));
        REQUIRE(bgzip_in.Tell() == -1);
        
        const char* buffer;
        int buffer_size = 0;
        while(buffer_size == 0) {
            // Fish for data until we get some
            REQUIRE(bgzip_in.Next((const void**)&buffer, &buffer_size));
        }
        
        // Make sure we got the right data that we skipped to
        REQUIRE(buffer[0] == TO_COMPRESS[10]);
    }


}

TEST_CASE("a BlockedGzipInputStream can read large amounts of non-blocked compressed data", "[bgzip]") {
    stringstream datastream;

    {
        // Write some data in
        ::google::protobuf::io::OstreamOutputStream raw_out(&datastream);
        ::google::protobuf::io::GzipOutputStream gzip_out(&raw_out);
        ::google::protobuf::io::CodedOutputStream coded_out(&gzip_out);
        
        for (uint32_t i = 0; i < 1000000; i++) {
            // Generate ~4 MB of data
            coded_out.WriteLittleEndian32(i);
        }
        
    }
    
    // Stringstreams track put and get positions separately. So we will read from the beginning.
    REQUIRE(datastream.tellg() == 0);
    
    // Now try and read it back
    BlockedGzipInputStream bgzip_in(datastream);
    ::google::protobuf::io::CodedInputStream coded_in(&bgzip_in);
    
    uint32_t expected = 0;
    uint32_t found;
    
    while(coded_in.ReadLittleEndian32(&found)) {
        REQUIRE(found == expected);
        expected++;
    }
    
    REQUIRE(expected == 1000000);

}

TEST_CASE("a BlockedGzipInputStream can read large amounts of blocked compressed data", "[bgzip]") {
    stringstream datastream;

    {
        // Write some data in
        BlockedGzipOutputStream bgzip_out(datastream);
        ::google::protobuf::io::CodedOutputStream coded_out(&bgzip_out);
        
        for (uint32_t i = 0; i < 1000000; i++) {
            // Generate ~4 MB of data
            coded_out.WriteLittleEndian32(i);
        }
        
    }
    
    // Stringstreams track put and get positions separately. So we will read from the beginning.
    REQUIRE(datastream.tellg() == 0);
    
    // Now try and read it back
    BlockedGzipInputStream bgzip_in(datastream);
    ::google::protobuf::io::CodedInputStream coded_in(&bgzip_in);
    
    uint32_t expected = 0;
    uint32_t found;
    
    while(coded_in.ReadLittleEndian32(&found)) {
        REQUIRE(found == expected);
        expected++;
    }
    
    REQUIRE(expected == 1000000);

}

}

}
