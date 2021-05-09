#include "address.h"
#include "config.h"

#include <yt/yt/core/concurrency/action_queue.h>
#include <yt/yt/core/concurrency/periodic_executor.h>
#include <yt/yt/core/concurrency/spinlock.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/core/net/dns_resolver.h>
#include <yt/yt/core/net/local_address.h>

#include <yt/yt/core/misc/async_expiring_cache.h>
#include <yt/yt/core/misc/fs.h>
#include <yt/yt/core/misc/shutdown.h>

#include <yt/yt/core/profiling/timing.h>

#include <util/generic/singleton.h>
#include <util/string/hex.h>

#ifdef _win_
    #include <ws2ipdef.h>
    #include <winsock2.h>
#else
    #include <ifaddrs.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <unistd.h>
#endif

namespace NYT::NNet {

using namespace NConcurrency;
using namespace NYson;
using namespace NYTree;

using ::ToString;

////////////////////////////////////////////////////////////////////////////////

static const NLogging::TLogger Logger("Network");

////////////////////////////////////////////////////////////////////////////////

TString BuildServiceAddress(TStringBuf hostName, int port)
{
    return Format("%v:%v", hostName, port);
}

void ParseServiceAddress(TStringBuf address, TStringBuf* hostName, int* port)
{
    auto colonIndex = address.find_last_of(':');
    if (colonIndex == TString::npos) {
        THROW_ERROR_EXCEPTION("Service address %Qv is malformed, <host>:<port> format is expected",
            address);
    }

    if (hostName) {
        *hostName = address.substr(0, colonIndex);
    }

    if (port) {
        try {
            *port = FromString<int>(address.substr(colonIndex + 1));
        } catch (const std::exception) {
            THROW_ERROR_EXCEPTION("Port number in service address %Qv is malformed",
                address);
        }
    }
}

int GetServicePort(TStringBuf address)
{
    int result;
    ParseServiceAddress(address, nullptr, &result);
    return result;
}

TStringBuf GetServiceHostName(TStringBuf address)
{
    TStringBuf result;
    ParseServiceAddress(address, &result, nullptr);
    return result;
}

////////////////////////////////////////////////////////////////////////////////

const TNetworkAddress NullNetworkAddress;

TNetworkAddress::TNetworkAddress()
{
    Storage = {};
    Storage.ss_family = AF_UNSPEC;
    Length = sizeof(Storage);
}

TNetworkAddress::TNetworkAddress(const TNetworkAddress& other, int port)
{
    memcpy(&Storage, &other.Storage, sizeof(Storage));
    switch (Storage.ss_family) {
        case AF_INET:
            reinterpret_cast<sockaddr_in*>(&Storage)->sin_port = htons(port);
            Length = sizeof(sockaddr_in);
            break;
        case AF_INET6:
            reinterpret_cast<sockaddr_in6*>(&Storage)->sin6_port = htons(port);
            Length = sizeof(sockaddr_in6);
            break;
        default:
            YT_ABORT();
    }
}

TNetworkAddress::TNetworkAddress(const sockaddr& other, socklen_t length)
{
    Length = length == 0 ? GetGenericLength(other) : length;
    memcpy(&Storage, &other, Length);
}

TNetworkAddress::TNetworkAddress(int family, const char* addr, size_t size)
{
    Storage = {};
    Storage.ss_family = family;
    switch (Storage.ss_family) {
        case AF_INET: {
            auto* typedSockAddr = reinterpret_cast<sockaddr_in*>(&Storage);
            YT_ASSERT(size <= sizeof(sockaddr_in));
            memcpy(&typedSockAddr->sin_addr, addr, size);
            Length = sizeof(sockaddr_in);
            break;
        }
        case AF_INET6: {
            auto* typedSockAddr = reinterpret_cast<sockaddr_in6*>(&Storage);
            YT_ASSERT(size <= sizeof(sockaddr_in6));
            memcpy(&typedSockAddr->sin6_addr, addr, size);
            Length = sizeof(sockaddr_in6);
            break;
        }
        default:
            YT_ABORT();
    }
}

sockaddr* TNetworkAddress::GetSockAddr()
{
    return reinterpret_cast<sockaddr*>(&Storage);
}

const sockaddr* TNetworkAddress::GetSockAddr() const
{
    return reinterpret_cast<const sockaddr*>(&Storage);
}

socklen_t TNetworkAddress::GetGenericLength(const sockaddr& sockAddr)
{
    switch (sockAddr.sa_family) {
#ifdef _unix_
        case AF_UNIX:
            return sizeof (sockaddr_un);
#endif
        case AF_INET:
            return sizeof (sockaddr_in);
        case AF_INET6:
            return sizeof (sockaddr_in6);
        default:
            // Don't know its actual size, report the maximum possible.
            return sizeof (sockaddr_storage);
    }
}

ui16 TNetworkAddress::GetPort() const
{
    switch (Storage.ss_family) {
        case AF_INET:
            return ntohs(reinterpret_cast<const sockaddr_in*>(&Storage)->sin_port);
        case AF_INET6:
            return ntohs(reinterpret_cast<const sockaddr_in6*>(&Storage)->sin6_port);
        default:
            THROW_ERROR_EXCEPTION("Address has no port");
    }
}

bool TNetworkAddress::IsUnix() const
{
    return Storage.ss_family == AF_UNIX;
}

bool TNetworkAddress::IsIP() const
{
    return IsIP4() || IsIP6();
}

bool TNetworkAddress::IsIP4() const
{
    return Storage.ss_family == AF_INET;
}

bool TNetworkAddress::IsIP6() const
{
    return Storage.ss_family == AF_INET6;
}

TIP6Address TNetworkAddress::ToIP6Address() const
{
    if (Storage.ss_family != AF_INET6) {
        THROW_ERROR_EXCEPTION("Address is not an IPv6 address");
    }

    auto addr = reinterpret_cast<const sockaddr_in6*>(&Storage)->sin6_addr;
    std::reverse(addr.s6_addr, addr.s6_addr + sizeof(addr));
    return TIP6Address::FromRawBytes(addr.s6_addr);
}

socklen_t TNetworkAddress::GetLength() const
{
    return Length;
}

socklen_t* TNetworkAddress::GetLengthPtr()
{
    return &Length;
}

TErrorOr<TNetworkAddress> TNetworkAddress::TryParse(TStringBuf address)
{
    TString ipAddress(address);
    std::optional<int> port;

    auto closingBracketIndex = address.find(']');
    if (closingBracketIndex != TString::npos) {
        if (closingBracketIndex == TString::npos || address.empty() || address[0] != '[') {
            return TError("Address %Qv is malformed, expected [<addr>]:<port> or [<addr>] format",
                address);
        }

        auto colonIndex = address.find(':', closingBracketIndex + 1);
        if (colonIndex != TString::npos) {
            try {
                port = FromString<int>(address.substr(colonIndex + 1));
            } catch (const std::exception) {
                return TError("Port number in address %Qv is malformed",
                    address);
            }
        }

        ipAddress = TString(address.substr(1, closingBracketIndex - 1));
    } else {
        if (address.find('.') != TString::npos) {
            auto colonIndex = address.find(':', closingBracketIndex + 1);
            if (colonIndex != TString::npos) {
                try {
                    port = FromString<int>(address.substr(colonIndex + 1));
                    ipAddress = TString(address.substr(0, colonIndex));
                } catch (const std::exception) {
                    return TError("Port number in address %Qv is malformed",
                        address);
                }
            }
        }
    }

    {
        // Try to parse as ipv4.
        struct sockaddr_in sa = {};
        if (inet_pton(AF_INET, ipAddress.c_str(), &sa.sin_addr) == 1) {
            if (port) {
                sa.sin_port = htons(*port);
            }
            sa.sin_family = AF_INET;
            return TNetworkAddress(*reinterpret_cast<sockaddr*>(&sa));
        }
    }
    {
        // Try to parse as ipv6.
        struct sockaddr_in6 sa = {};
        if (inet_pton(AF_INET6, ipAddress.c_str(), &(sa.sin6_addr))) {
            if (port) {
                sa.sin6_port = htons(*port);
            }
            sa.sin6_family = AF_INET6;
            return TNetworkAddress(*reinterpret_cast<sockaddr*>(&sa));
        }
    }

    return TError("Address %Qv is neither a valid IPv4 nor a valid IPv6 address",
        ipAddress);
}

TNetworkAddress TNetworkAddress::CreateIPv6Any(int port)
{
    sockaddr_in6 serverAddress = {};
    serverAddress.sin6_family = AF_INET6;
    serverAddress.sin6_addr = in6addr_any;
    serverAddress.sin6_port = htons(port);

    return TNetworkAddress(reinterpret_cast<const sockaddr&>(serverAddress), sizeof(serverAddress));
}

TNetworkAddress TNetworkAddress::CreateIPv6Loopback(int port)
{
    sockaddr_in6 serverAddress = {};
    serverAddress.sin6_family = AF_INET6;
    serverAddress.sin6_addr = in6addr_loopback;
    serverAddress.sin6_port = htons(port);

    return TNetworkAddress(reinterpret_cast<const sockaddr&>(serverAddress), sizeof(serverAddress));
}

TNetworkAddress TNetworkAddress::CreateUnixDomainSocketAddress(const TString& socketPath)
{
#ifdef _linux_
    // Abstract unix sockets are supported only on Linux.
    sockaddr_un sockAddr = {};
    if (socketPath.size() > sizeof(sockAddr.sun_path)) {
        THROW_ERROR_EXCEPTION("Unix domain socket path is too long")
            << TErrorAttribute("socket_path", socketPath)
            << TErrorAttribute("max_socket_path_length", sizeof(sockAddr.sun_path));
    }

    sockAddr.sun_family = AF_UNIX;
    memcpy(sockAddr.sun_path, socketPath.data(), socketPath.length());
    return TNetworkAddress(
        *reinterpret_cast<sockaddr*>(&sockAddr),
        sizeof (sockAddr.sun_family) +
        socketPath.length());
#else
    Y_UNUSED(socketPath);
    YT_ABORT();
#endif
}

TNetworkAddress TNetworkAddress::CreateAbstractUnixDomainSocketAddress(const TString& socketName)
{
    return CreateUnixDomainSocketAddress(TString("\0", 1) + socketName);
}

TNetworkAddress TNetworkAddress::Parse(TStringBuf address)
{
    return TryParse(address).ValueOrThrow();
}

TString ToString(const TNetworkAddress& address, const TNetworkAddressFormatOptions& options)
{
    const auto& sockAddr = address.GetSockAddr();

    const void* ipAddr;
    int port = 0;
    bool ipv6 = false;
    switch (sockAddr->sa_family) {
#ifdef _unix_
        case AF_UNIX: {
            const auto* typedAddr = reinterpret_cast<const sockaddr_un*>(sockAddr);
            //! See `man unix`.
            if (address.GetLength() == sizeof(sa_family_t)) {
                return "unix://[*unnamed*]";
            } else if (typedAddr->sun_path[0] == 0) {
                auto addressRef = TStringBuf(typedAddr->sun_path + 1, address.GetLength() - 1 - sizeof(sa_family_t));
                auto quoted = Format("%Qv", addressRef);
                return Format("unix://[%v]", quoted.substr(1, quoted.size() - 2));
            } else {
                auto addressRef = TString(typedAddr->sun_path, address.GetLength() - sizeof(sa_family_t));
                return Format("unix://%v", NFS::GetRealPath(addressRef));
            }
        }
#endif
        case AF_INET: {
            const auto* typedAddr = reinterpret_cast<const sockaddr_in*>(sockAddr);
            ipAddr = &typedAddr->sin_addr;
            port = typedAddr->sin_port;
            ipv6 = false;
            break;
        }
        case AF_INET6: {
            const auto* typedAddr = reinterpret_cast<const sockaddr_in6*>(sockAddr);
            ipAddr = &typedAddr->sin6_addr;
            port = typedAddr->sin6_port;
            ipv6 = true;
            break;
        }
        default:
            return Format("unknown://family(%v)", sockAddr->sa_family);
    }

    std::array<char, std::max(INET6_ADDRSTRLEN, INET_ADDRSTRLEN)> buffer;
    YT_VERIFY(inet_ntop(
        sockAddr->sa_family,
        const_cast<void*>(ipAddr),
        buffer.data(),
        buffer.size()));

    TStringBuilder result;
    if (options.IncludeTcpProtocol) {
        result.AppendString(TStringBuf("tcp://"));
    }

    bool withBrackets = ipv6 && (options.IncludeTcpProtocol || options.IncludePort);
    if (withBrackets) {
        result.AppendChar('[');
    }

    result.AppendString(buffer.data());

    if (withBrackets) {
        result.AppendChar(']');
    }

    if (options.IncludePort) {
        result.AppendFormat(":%v", ntohs(port));
    }

    return result.Flush();
}

bool operator == (const TNetworkAddress& lhs, const TNetworkAddress& rhs)
{
    auto lhsAddr = lhs.GetSockAddr();
    auto lhsSize = lhs.GetLength();

    auto rhsAddr = rhs.GetSockAddr();
    auto rhsSize = rhs.GetLength();

    TStringBuf rawLhs{reinterpret_cast<const char*>(lhsAddr), lhsSize};
    TStringBuf rawRhs{reinterpret_cast<const char*>(rhsAddr), rhsSize};

    return rawLhs == rawRhs;
}

bool operator != (const TNetworkAddress& lhs, const TNetworkAddress& rhs)
{
    return !(lhs == rhs);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

//! Parse project id notation as described here:
//! https://wiki.yandex-team.ru/noc/newnetwork/hbf/projectid/#faervolnajazapissetevogoprefiksasprojectidirabotasnim
bool ParseProjectId(TStringBuf* str, std::optional<ui32>* projectId)
{
    auto pos = str->find('@');
    if (pos == TStringBuf::npos) {
        // Project id not specified.
        return true;
    }

    if (pos == 0 || pos > 8) {
        // Project id occupies 32 bits of address, so it must be between 1 and 8 hex digits.
        return false;
    }

    ui32 value = 0;
    for (int i = 0; i < static_cast<int>(pos); ++i) {
        int digit = Char2DigitTable[static_cast<unsigned char>((*str)[i])];
        if (digit == '\xff') {
            return false;
        }

        value <<= 4;
        value += digit;
    }

    *projectId = value;
    str->Skip(pos + 1);
    return true;
}

bool ParseIP6Address(TStringBuf* str, TIP6Address* address)
{
    auto tokenizeWord = [&] (ui16* word) -> bool {
        int partLen = 0;
        ui16 wordValue = 0;

        if (str->empty()) {
            return false;
        }

        while (partLen < 4 && !str->empty()) {
            int digit = Char2DigitTable[static_cast<unsigned char>((*str)[0])];
            if (digit == '\xff' && partLen == 0) {
                return false;
            }
            if (digit == '\xff') {
                break;
            }

            str->Skip(1);
            wordValue <<= 4;
            wordValue += digit;
            ++partLen;
        }

        *word = wordValue;
        return true;
    };

    bool beforeAbbrev = true;
    int wordIndex = 0;
    int wordsPushed = 0;

    auto words = address->GetRawWords();
    std::fill_n(address->GetRawBytes(), TIP6Address::ByteSize, 0);

    auto isEnd = [&] () {
        return str->empty() || (*str)[0] == '/';
    };

    auto tokenizeAbbrev = [&] () {
        if (str->size() >= 2 && (*str)[0] == ':' && (*str)[1] == ':') {
            str->Skip(2);
            return true;
        }
        return false;
    };

    if (tokenizeAbbrev()) {
        beforeAbbrev = false;
        ++wordIndex;
    }

    if (isEnd() && !beforeAbbrev) {
        return true;
    }

    while (true) {
        if (beforeAbbrev) {
            ui16 newWord = 0;
            if (!tokenizeWord(&newWord)) {
                return false;
            }

            words[7 - wordIndex] = newWord;
            ++wordIndex;
        } else {
            ui16 newWord = 0;
            if (!tokenizeWord(&newWord)) {
                return false;
            }

            std::copy_backward(words, words + wordsPushed, words + wordsPushed + 1);
            words[0] = newWord;
            ++wordsPushed;
        }

        // end of full address
        if (wordIndex + wordsPushed == 8) {
            break;
        }

        // end of abbreviated address
        if (isEnd() && !beforeAbbrev) {
            break;
        }

        // ':' or '::'
        if (beforeAbbrev && tokenizeAbbrev()) {
            beforeAbbrev = false;
            ++wordIndex;

            if (isEnd()) {
                break;
            }
        } else if (!str->empty() && (*str)[0] == ':') {
            str->Skip(1);
        } else {
            return false;
        }
    }

    return true;
}

bool ParseMask(TStringBuf buf, int* maskSize)
{
    if (buf.size() < 2 || buf[0] != '/') {
        return false;
    }

    *maskSize = 0;
    for (int i = 1; i < 4; ++i) {
        if (i == std::ssize(buf)) {
            return true;
        }

        if (buf[i] < '0' || '9' < buf[i]) {
            return false;
        }

        *maskSize = (*maskSize * 10) + (buf[i] - '0');
    }

    return buf.size() == 4 && *maskSize <= 128;
}

} // namespace

const ui8* TIP6Address::GetRawBytes() const
{
    return Raw_.data();
}

ui8* TIP6Address::GetRawBytes()
{
    return Raw_.data();
}

const ui16* TIP6Address::GetRawWords() const
{
    return reinterpret_cast<const ui16*>(GetRawBytes());
}

ui16* TIP6Address::GetRawWords()
{
    return reinterpret_cast<ui16*>(GetRawBytes());
}

const ui32* TIP6Address::GetRawDWords() const
{
    return reinterpret_cast<const ui32*>(GetRawBytes());
}

ui32* TIP6Address::GetRawDWords()
{
    return reinterpret_cast<ui32*>(GetRawBytes());
}

TIP6Address TIP6Address::FromRawBytes(const ui8* raw)
{
    TIP6Address result;
    ::memcpy(result.Raw_.data(), raw, ByteSize);
    return result;
}

TIP6Address TIP6Address::FromRawWords(const ui16* raw)
{
    return FromRawBytes(reinterpret_cast<const ui8*>(raw));
}

TIP6Address TIP6Address::FromRawDWords(const ui32* raw)
{
    return FromRawBytes(reinterpret_cast<const ui8*>(raw));
}

TIP6Address TIP6Address::FromString(TStringBuf str)
{
    TIP6Address result;
    if (!FromString(str, &result)) {
        THROW_ERROR_EXCEPTION("Error parsing IP6 address %Qv", str);
    }
    return result;
}

bool TIP6Address::FromString(TStringBuf str, TIP6Address* address)
{
    TStringBuf buf = str;
    if (!ParseIP6Address(&buf, address) || !buf.empty()) {
        return false;
    }
    return true;
}

void FormatValue(TStringBuilderBase* builder, const TIP6Address& address, TStringBuf /*spec*/)
{
    const auto* parts = reinterpret_cast<const ui16*>(address.GetRawBytes());
    std::pair<int, int> maxRun = {-1, -1};
    int start = -1;
    int end = -1;
    auto endRun = [&] () {
        if ((end - start) >= (maxRun.second - maxRun.first) && (end - start) > 1) {
            maxRun = {start, end};
        }

        start = -1;
        end = -1;
    };

    for (int index = 0; index < 8; ++index) {
        if (parts[index] == 0) {
            if (start == -1) {
                start = index;
            }
            end = index + 1;
        } else {
            endRun();
        }
    }
    endRun();

    for (int index = 7; index >= 0; --index) {
        if (maxRun.first <= index && index < maxRun.second) {
            if (index == maxRun.first) {
                builder->AppendChar(':');
                builder->AppendChar(':');
            }
        } else {
            if (index != 7 && index + 1 != maxRun.first) {
                builder->AppendChar(':');
            }
            builder->AppendFormat("%x", parts[index]);
        }
    }
}

TString ToString(const TIP6Address& address)
{
    return ToStringViaBuilder(address);
}

bool operator == (const TIP6Address& lhs, const TIP6Address& rhs)
{
    return ::memcmp(lhs.GetRawBytes(), rhs.GetRawBytes(), TIP6Address::ByteSize) == 0;
}

bool operator != (const TIP6Address& lhs, const TIP6Address& rhs)
{
    return !(lhs == rhs);
}

TIP6Address operator|(const TIP6Address& lhs, const TIP6Address& rhs)
{
    auto result = lhs;
    result |= rhs;
    return result;
}

TIP6Address operator&(const TIP6Address& lhs, const TIP6Address& rhs)
{
    auto result = lhs;
    result &= rhs;
    return result;
}

TIP6Address& operator|=(TIP6Address& lhs, const TIP6Address& rhs)
{
    *reinterpret_cast<ui64*>(lhs.GetRawBytes()) |= *reinterpret_cast<const ui64*>(rhs.GetRawBytes());
    *reinterpret_cast<ui64*>(lhs.GetRawBytes() + 8) |= *reinterpret_cast<const ui64*>(rhs.GetRawBytes() + 8);
    return lhs;
}

TIP6Address& operator&=(TIP6Address& lhs, const TIP6Address& rhs)
{
    *reinterpret_cast<ui64*>(lhs.GetRawBytes()) &= *reinterpret_cast<const ui64*>(rhs.GetRawBytes());
    *reinterpret_cast<ui64*>(lhs.GetRawBytes() + 8) &= *reinterpret_cast<const ui64*>(rhs.GetRawBytes() + 8);
    return lhs;
}

void Deserialize(TIP6Address& value, INodePtr node)
{
    value = TIP6Address::FromString(node->AsString()->GetValue());
}

void Serialize(const TIP6Address& value, IYsonConsumer* consumer)
{
    consumer->OnStringScalar(ToString(value));
}

////////////////////////////////////////////////////////////////////////////////

TIP6Network::TIP6Network(const TIP6Address& network, const TIP6Address& mask)
    : Network_(network)
    , Mask_(mask)
{ }

const TIP6Address& TIP6Network::GetAddress() const
{
    return Network_;
}

const TIP6Address& TIP6Network::GetMask() const
{
    return Mask_;
}

int TIP6Network::GetMaskSize() const
{
    int size = 0;
    const auto* parts = Mask_.GetRawDWords();
    for (size_t partIndex = 0; partIndex < 4; ++partIndex) {
        size += __builtin_popcount(parts[partIndex]);
    }
    return size;
}

bool TIP6Network::Contains(const TIP6Address& address) const
{
    TIP6Address masked = address;
    masked &= Mask_;
    return masked == Network_;
}

TIP6Network TIP6Network::FromString(TStringBuf str)
{
    TIP6Network network;
    if (!FromString(str, &network)) {
        THROW_ERROR_EXCEPTION("Error parsing IP6 network %Qv", str);
    }
    return network;
}

bool TIP6Network::FromString(TStringBuf str, TIP6Network* network)
{
    auto buf = str;
    std::optional<ui32> projectId = std::nullopt;
    if (!ParseProjectId(&buf, &projectId)) {
        return false;
    }

    if (!ParseIP6Address(&buf, &network->Network_)) {
        return false;
    }

    int maskSize = 0;
    if (!ParseMask(buf, &maskSize)) {
        return false;
    }

    if (projectId) {
        network->Network_.GetRawWords()[2] = *projectId;
        network->Network_.GetRawWords()[3] = *projectId >> 16;
    }

    network->Mask_ = TIP6Address();
    auto bytes = network->Mask_.GetRawBytes();
    for (int i = 0; i < static_cast<int>(TIP6Address::ByteSize * 8); ++i) {
        if (i >= static_cast<int>(TIP6Address::ByteSize * 8) - maskSize) {
            *(bytes + i / 8) |= (1 << (i % 8));
        } else {
            *(bytes + i / 8) &= ~(1 << (i % 8));
        }
    }

    if (projectId) {
        static_assert(TIP6Address::ByteSize == 16);
        network->Mask_.GetRawDWords()[1] = 0xffffffff;
    }

    return true;
}

void FormatValue(TStringBuilderBase* builder, const TIP6Network& network, TStringBuf /*spec*/)
{
    builder->AppendFormat("%v/%v",
        network.GetAddress(),
        network.GetMaskSize());
}

TString ToString(const TIP6Network& network)
{
    return ToStringViaBuilder(network);
}

void Deserialize(TIP6Network& value, INodePtr node)
{
    value = TIP6Network::FromString(node->AsString()->GetValue());
}

void Serialize(const TIP6Network& value, IYsonConsumer* consumer)
{
    consumer->OnStringScalar(ToString(value));
}

////////////////////////////////////////////////////////////////////////////////

//! Performs asynchronous host name resolution.
class TAddressResolver::TImpl
    : public virtual TRefCounted
    , private TAsyncExpiringCache<TString, TNetworkAddress>
{
public:
    explicit TImpl(TAddressResolverConfigPtr config);

    void Shutdown();

    TFuture<TNetworkAddress> Resolve(const TString& hostName);

    void EnsureLocalHostName();

    bool IsLocalAddress(const TNetworkAddress& address);

    void PurgeCache();

    void Configure(TAddressResolverConfigPtr config);

private:
    TAddressResolverConfigPtr Config_;

    std::atomic<bool> HasCachedLocalAddresses_ = {false};
    std::vector<TNetworkAddress> CachedLocalAddresses_;
    YT_DECLARE_SPINLOCK(TReaderWriterSpinLock, CacheLock_);

    const TActionQueuePtr Queue_ = New<TActionQueue>("AddressResolver");

    TDnsResolver DnsResolver_;

    virtual TFuture<TNetworkAddress> DoGet(const TString& hostName, bool isPeriodicUpdate) noexcept override;

    const std::vector<TNetworkAddress>& GetLocalAddresses();
};

////////////////////////////////////////////////////////////////////////////////

TAddressResolver::TImpl::TImpl(TAddressResolverConfigPtr config)
    : TAsyncExpiringCache(config)
    , DnsResolver_(
        config->Retries,
        config->ResolveTimeout,
        config->MaxResolveTimeout,
        config->WarningTimeout,
        config->Jitter)
{
    DnsResolver_.Start();
    Configure(std::move(config));
}

void TAddressResolver::TImpl::Shutdown()
{
    DnsResolver_.Stop();

    Queue_->Shutdown();
}

TFuture<TNetworkAddress> TAddressResolver::TImpl::Resolve(const TString& hostName)
{
    // Check if |address| parses into a valid IPv4 or IPv6 address.
    {
        auto result = TNetworkAddress::TryParse(hostName);
        if (result.IsOK()) {
            return MakeFuture(result);
        }
    }

    // Run async resolution.
    return Get(hostName);
}

TFuture<TNetworkAddress> TAddressResolver::TImpl::DoGet(const TString& hostname, bool /*isPeriodicUpdate*/) noexcept
{
    return DnsResolver_
        .ResolveName(hostname, Config_->EnableIPv4, Config_->EnableIPv6)
        .Apply(BIND([=, this_ = MakeStrong(this)] (const TErrorOr<TNetworkAddress>& result) {
            // Empty callback just to forward future callbacks into proper thread.
            return result.ValueOrThrow();
        })
        .AsyncVia(Queue_->GetInvoker()));
}

void TAddressResolver::TImpl::EnsureLocalHostName()
{
    if (Config_->LocalHostNameOverride) {
        return;
    }

    UpdateLocalHostName([] (const char* failedCall, const char* details) {
        THROW_ERROR_EXCEPTION("Error updating localhost name; %v failed: %v", failedCall, details);
    }, Config_->ResolveHostNameIntoFqdn);

    YT_LOG_INFO("Localhost name determined via system call (LocalHostName: %v, ResolveHostNameIntoFqdn: %v)",
        GetLocalHostName(),
        Config_->ResolveHostNameIntoFqdn);
}

bool TAddressResolver::TImpl::IsLocalAddress(const TNetworkAddress& address)
{
    TNetworkAddress localIP{address, 0};

    const auto& localAddresses = GetLocalAddresses();
    auto&& it = std::find(localAddresses.begin(), localAddresses.end(), localIP);
    auto jt = localAddresses.end();
    return it != jt;
}

const std::vector<TNetworkAddress>& TAddressResolver::TImpl::GetLocalAddresses()
{
    if (HasCachedLocalAddresses_) {
        return CachedLocalAddresses_;
    }

    std::vector<TNetworkAddress> localAddresses;

    struct ifaddrs* ifAddresses;
    if (getifaddrs(&ifAddresses) == -1) {
        auto error = TError("getifaddrs failed")
            << TError::FromSystem();
        YT_LOG_WARNING(error, "Failed to initialize local addresses");
    } else {
        auto holder = std::unique_ptr<ifaddrs, decltype(&freeifaddrs)>(ifAddresses, &freeifaddrs);

        for (const auto* currentAddress = ifAddresses;
            currentAddress;
            currentAddress = currentAddress->ifa_next)
        {
            if (currentAddress->ifa_addr == nullptr) {
                continue;
            }

            auto family = currentAddress->ifa_addr->sa_family;
            if (family != AF_INET && family != AF_INET6) {
                continue;
            }
            localAddresses.push_back(TNetworkAddress(*currentAddress->ifa_addr));
        }
    }

    {
        auto guard = WriterGuard(CacheLock_);
        // NB: Only update CachedLocalAddresses_ once.
        if (!HasCachedLocalAddresses_) {
            CachedLocalAddresses_ = std::move(localAddresses);
            HasCachedLocalAddresses_ = true;
        }
    }

    return CachedLocalAddresses_;
}

void TAddressResolver::TImpl::PurgeCache()
{
    Clear();
    YT_LOG_INFO("Address cache purged");
}

void TAddressResolver::TImpl::Configure(TAddressResolverConfigPtr config)
{
    Config_ = std::move(config);

    if (Config_->LocalHostNameOverride) {
        WriteLocalHostName(*Config_->LocalHostNameOverride);
        YT_LOG_INFO("Localhost name configured via config override (LocalHostName: %v)",
            Config_->LocalHostNameOverride);
    }
}

////////////////////////////////////////////////////////////////////////////////

TAddressResolver::TAddressResolver()
    : Impl_(New<TImpl>(New<TAddressResolverConfig>()))
{ }

TAddressResolver* TAddressResolver::Get()
{
    return LeakySingleton<TAddressResolver>();
}

void TAddressResolver::StaticShutdown()
{
    Get()->Shutdown();
}

void TAddressResolver::Shutdown()
{
    Impl_->Shutdown();
}

TFuture<TNetworkAddress> TAddressResolver::Resolve(const TString& address)
{
    return Impl_->Resolve(address);
}

void TAddressResolver::EnsureLocalHostName()
{
    return Impl_->EnsureLocalHostName();
}

bool TAddressResolver::IsLocalAddress(const TNetworkAddress& address)
{
    return Impl_->IsLocalAddress(address);
}

void TAddressResolver::PurgeCache()
{
    YT_ASSERT(Impl_);
    return Impl_->PurgeCache();
}

void TAddressResolver::Configure(TAddressResolverConfigPtr config)
{
    YT_ASSERT(Impl_);
    return Impl_->Configure(std::move(config));
}

////////////////////////////////////////////////////////////////////////////////

TMtnAddress::TMtnAddress(TIP6Address address)
    : Address_(address)
{ }

ui64 TMtnAddress::GetPrefix() const
{
    return GetBytesRangeValue(PrefixOffsetInBytes, TotalLenInBytes);
}

TMtnAddress& TMtnAddress::SetPrefix(ui64 prefix)
{
    SetBytesRangeValue(PrefixOffsetInBytes, TotalLenInBytes, prefix);
    return *this;
}

ui64 TMtnAddress::GetGeo() const
{
    return GetBytesRangeValue(GeoOffsetInBytes, PrefixOffsetInBytes);
}

TMtnAddress& TMtnAddress::SetGeo(ui64 geo)
{
    SetBytesRangeValue(GeoOffsetInBytes, PrefixOffsetInBytes, geo);
    return *this;
}

ui64 TMtnAddress::GetProjectId() const
{
    return GetBytesRangeValue(ProjectIdOffsetInBytes, GeoOffsetInBytes);
}

TMtnAddress& TMtnAddress::SetProjectId(ui64 projectId)
{
    SetBytesRangeValue(ProjectIdOffsetInBytes, GeoOffsetInBytes, projectId);
    return *this;
}

ui64 TMtnAddress::GetHost() const
{
    return GetBytesRangeValue(HostOffsetInBytes, ProjectIdOffsetInBytes);
}

TMtnAddress& TMtnAddress::SetHost(ui64 host)
{
    SetBytesRangeValue(HostOffsetInBytes, ProjectIdOffsetInBytes, host);
    return *this;
}

const TIP6Address& TMtnAddress::ToIP6Address() const
{
    return Address_;
}

ui64 TMtnAddress::GetBytesRangeValue(int leftIndex, int rightIndex) const
{
    if (leftIndex > rightIndex) {
        THROW_ERROR_EXCEPTION("Left index is greater than right index (LeftIndex: %v, RightIndex: %v)",
            leftIndex,
            rightIndex);
    }

    const auto* addressBytes = Address_.GetRawBytes();

    ui64 result = 0;
    for (int index = rightIndex - 1; index >= leftIndex; --index) {
        result = (result << 8) | addressBytes[index];
    }
    return result;
}

void TMtnAddress::SetBytesRangeValue(int leftIndex, int rightIndex, ui64 value)
{
    if (leftIndex > rightIndex) {
        THROW_ERROR_EXCEPTION("Left index is greater than right index (LeftIndex: %v, RightIndex: %v)",
            leftIndex,
            rightIndex);
    }

    auto bytesInRange = rightIndex - leftIndex;
    if (value >= (1ull << (8 * bytesInRange))) {
        THROW_ERROR_EXCEPTION("Value is too large to be set in [leftIndex; rightIndex) interval (LeftIndex: %v, RightIndex: %v, Value %v)",
            leftIndex,
            rightIndex,
            value);
    }

    auto* addressBytes = Address_.GetRawBytes();
    for (int index = leftIndex; index < rightIndex; ++index) {
        addressBytes[index] = value & 255;
        value >>= 8;
    }
}

////////////////////////////////////////////////////////////////////////////////

REGISTER_SHUTDOWN_CALLBACK(2, TAddressResolver::StaticShutdown);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNet

