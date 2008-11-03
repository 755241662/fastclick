#ifndef CLICK_ARPTABLE_HH
#define CLICK_ARPTABLE_HH
#include <click/element.hh>
#include <click/etheraddress.hh>
#include <click/hashcontainer.hh>
#include <click/hashallocator.hh>
#include <click/sync.hh>
#include <click/timer.hh>
#include <click/list.hh>
CLICK_DECLS

/*
=c

ARPTable(I<keywords>)

=s arp

stores IP-to-Ethernet mappings

=d

The ARPTable element stores IP-to-Ethernet mappings, such as are useful for
the ARP protocol.  ARPTable is an information element, with no inputs or
outputs.  ARPQuerier normally encapsulates access to an ARPTable element.  A
separate ARPTable is useful if several ARPQuerier elements should share a
table.

Keyword arguments are:

=over 8

=item CAPACITY

Unsigned integer.  The maximum number of saved IP packets the ARPTable will
hold at a time.  Default is 2048; zero means unlimited.

=item ENTRY_CAPACITY

Unsigned integer.  The maximum number of ARP entries the ARPTable will hold at
a time.  Default is zero, which means unlimited.

=item TIMEOUT

Time value.  The amount of time after which an ARP entry will expire.  Default
is 5 minutes.  Zero means ARP entries never expires.

=h table r

Return a table of the ARP entries.  The returned string has four
space-separated columns: an IP address, whether the entry is valid (1 means
valid, 0 means not), the corresponding Ethernet address, and finally, the
amount of time since the entry was last updated.

=h drops r

Return the number of packets dropped because of timeouts or capacity limits.

=h insert w

Add an entry to the table.  The format should be "IP ETH".

=h delete w

Delete an entry from the table.  The string should consist of an IP address.

=h clear w

Clear the table, deleting all entries.

=a

ARPQuerier
*/

class ARPTable : public Element { public:
  
    ARPTable();
    ~ARPTable();
  
    const char *class_name() const		{ return "ARPTable"; }

    int configure(Vector<String> &, ErrorHandler *);
    bool can_live_reconfigure() const		{ return true; }
    void take_state(Element *, ErrorHandler *);
    void add_handlers();
    void cleanup(CleanupStage);
    
    int lookup(IPAddress ip, EtherAddress *eth, click_jiffies_t poll_jiffies);
    EtherAddress lookup(IPAddress ip);
    IPAddress reverse_lookup(const EtherAddress &eth);
    int insert(IPAddress ip, const EtherAddress &en, Packet **head = 0);
    int append_query(IPAddress ip, Packet *p);
    void clear();

    uint32_t capacity() const {
	return _packet_capacity;
    }
    void set_capacity(uint32_t capacity) {
	_packet_capacity = capacity;
    }
    uint32_t entry_capacity() const {
	return _entry_capacity;
    }
    void set_entry_capacity(uint32_t entry_capacity) {
	_entry_capacity = entry_capacity;
    }
    Timestamp timeout() const {
	return Timestamp::make_jiffies(_expire_jiffies);
    }
    void set_timeout(const Timestamp &timeout) {
	if (timeout.sec() < 0
	    || (click_jiffies_t) timeout.sec() > (click_jiffies_t) -1 / CLICK_HZ)
	    _expire_jiffies = 0;
	else
	    _expire_jiffies = timeout.jiffies();
    }
    uint32_t drops() const {
	return _drops;
    }

    void run_timer(Timer *);
    
    enum {
	h_table, h_insert, h_delete, h_clear
    };
    static String read_handler(Element *e, void *user_data);
    static int write_handler(const String &str, Element *e, void *user_data, ErrorHandler *errh);
    
    struct ARPEntry {		// This structure is now larger than I'd like
	IPAddress _ip;		// (40B) but probably still fine.
	ARPEntry *_hashnext;
	EtherAddress _eth;
	bool _unicast;
	click_jiffies_t _live_jiffies;
	click_jiffies_t _poll_jiffies;
	Packet *_head;
	Packet *_tail;
	List_member<ARPEntry> _age_link;
	typedef IPAddress key_type;
	typedef IPAddress key_const_reference;
	key_const_reference hashkey() const {
	    return _ip;
	}
	bool expired(click_jiffies_t now, uint32_t expire_jiffies) const {
	    return click_jiffies_less(_live_jiffies + expire_jiffies, now)
		&& expire_jiffies;
	}
	bool unicast(click_jiffies_t now, uint32_t expire_jiffies) const {
	    return _unicast && !expired(now, expire_jiffies);
	}
	ARPEntry(IPAddress ip)
	    : _ip(ip), _hashnext(), _eth(EtherAddress::make_broadcast()),
	      _unicast(false), _head(), _tail() {
	}
    };

  private:
    
    ReadWriteLock _lock;

    typedef HashContainer<ARPEntry> Table;
    Table _table;
    typedef List<ARPEntry, &ARPEntry::_age_link> AgeList;
    AgeList _age;
    atomic_uint32_t _entry_count;
    atomic_uint32_t _packet_count;
    uint32_t _entry_capacity;
    uint32_t _packet_capacity;
    uint32_t _expire_jiffies;
    atomic_uint32_t _drops;
    HashAllocator<sizeof(ARPEntry)> _alloc;
    Timer _expire_timer;

    ARPEntry *ensure(IPAddress ip);
    void slim();
    
};

inline int
ARPTable::lookup(IPAddress ip, EtherAddress *eth, click_jiffies_t poll_jiffies)
{
    _lock.acquire_read();
    int r = -1;
    if (Table::iterator it = _table.find(ip)) {
	click_jiffies_t now = click_jiffies();
	if (!it->expired(now, _expire_jiffies)) {
	    *eth = it->_eth;
	    if (poll_jiffies
		&& !click_jiffies_less(now, it->_live_jiffies + poll_jiffies)
		&& !click_jiffies_less(now, it->_poll_jiffies + (CLICK_HZ / 10))) {
		it->_poll_jiffies = now;
		r = 1;
	    } else
		r = 0;
	}
    }
    _lock.release_read();
    return r;
}

inline EtherAddress
ARPTable::lookup(IPAddress ip)
{
    EtherAddress eth;
    if (lookup(ip, &eth, 0) >= 0)
	return eth;
    else
	return EtherAddress::make_broadcast();
}

CLICK_ENDDECLS
#endif