// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors. The
//   software is licensed under the Tilera MDE License.
//
//   Unless otherwise agreed by Tilera in writing, you may not remove or
//   alter this notice or any other notice embedded in Materials by Tilera
//   or Tilera's suppliers or licensors in any way.
//
//


// Basically, this classifier is based around the concept of "rules",
// one of which is selected based on the channel, dmac, and vlan of
// incoming packets, and then the rule determines the bucket, offset,
// and buffer stack to be used for the packet.
//
// Each rule specifies "bucket" information, which is combined with
// the "flow hash" to determine the actual bucket.
//
// Each rule specifies a headroom and a tailroom, and a capacity.  If
// the total size (packet size plus headroom plus tailroom) exceed the
// capacity, the packet will be dropped.  Otherwise, the buffer stack
// will be chosen from an array of eight buffer stacks provided by the
// rule, based on which standard buffer size index (128, 256, 512,
// 1024, 1664, 4096, 10368, or 65535) can contain the total size.  The
// offset will be the headroom (plus any required chaining pointer).
//
// The capacity can be used to ensure that chaining will not occur
// even if an unexpectedly large packet arrives.
//
// For example, a rule could specify a headroom of 66, and a tailroom
// of 32, and then could request that packets of up to 512 - (66 + 32)
// bytes should use a buffer stack of 512 byte buffers, while packets
// of up to 1664 - (66 + 32) bytes should use a buffer stack of 1664
// byte buffers.  It could specify a capacity of 1664 to force larger
// packets to be dropped, or could specify a larger capacity to allow
// chaining to be done using the 1664 byte buffers.
//
// Note that chaining does not interact very well with tailroom.
//
// Note that an application could also supply extra "headroom" and
// "tailroom" by simply creating buffers larger than needed, and
// supplying an adjusted pointer, and in fact, this can be useful when
// more than 255 bytes of tailroom are required.  But note that this
// complicates the application, and does NOT allow simple forwarding
// of packets after extending their header.
//
//
// Actual "idesc" (aka "pDesc") contents (in little endian order):
//
//   0x00 - 0x00: ring
//   0x01 - 0x01: channel + me + tr
//   0x02 - 0x03: size + ce + ct
//   0x04 - 0x05: bucket
//   0x06 - 0x06: flags (cs + nr + dest + sq + ts + ps + be)
//   0x07 - 0x07: ctr0
//   0x08 - 0x08: ctr1
//   0x09 - 0x09: csum_start
//   0x0A - 0x0B: csum_seed (becomes csum_val)
//   0x0C - 0x0F: custom0: flow hash
//   0x10 - 0x11: custom1: vlan (or 0xFFFF)
//   0x12 - 0x13: custom1: ethertype
//   0x14 - 0x14: custom1: l2_offset (for all packets)
//   0x15 - 0x15: custom1: l3_offset (for most packets)
//   0x16 - 0x16: custom1: l4_offset (for some packets, else zero)
//   0x17 - 0x17: custom1: status (see below)
//   0x18 - 0x1F: custom2: dmac info (optional)
//   0x20 - 0x27: custom3: smac info (optional)
//   0x28 - 0x29: gp_sqn_sel (becomes gp_sqn)
//   0x2A - 0x2F: packet_sqn
//   0x30 - 0x33: time_stamp_ns
//   0x34 - 0x37: time_stamp_sec
//   0x38 - 0x3F: buffer info (0x38 is offset, 0x3E is stack)
//
// The "status" byte consists of flags:
//   0x80 = bad packet (e.g. bad IPv4 header checksum)
//
// ISSUE: Are there any other useful bits to store in "status"?
// Maybe a bit for "multicast"?
// Maybe a bit or two for "flow hash variety"?
// Maybe a bit or two for "tcp/udp"?
// Maybe a bit for "CUSTOM_MAC_INFO"?
//
// ISSUE: Is there any other useful "standard" metadata?
//
// Since the "idesc" (aka "pDesc") starts out zero'd, and thus the
// "flags", and thus "dest", is zero, we can "drop" a packet by simply
// calling "return", until "flags" has been written (near the end).
//
// WARNING: Some of the tables below are initialized with "bogus"
// values for testing (including most of the "zeroed" arrays).
//
//
// Note that there is a 2 cycle stall after every mis-predicted
// jump, and a 1 cycle stall after every correctly predicted taken
// jump, and there are 3 stall slots between mseek() and memN().
//
// Currently the classifier takes 171 cycles to handle a IPv4 UDP packet
// with Q-in-Q and SNAP, with no IPv4 options or payload, if there are
// no dmac/vlan conflicts, of which about 25 cycles are "stalls", almost
// entirely "jump" stalls.
//
//
// There is no hard limit on the number of cycles the classifier can
// spend on any packet, but in order to handle min-packets (60 bytes
// plus 24 bytes of ethernet overhead) at 40G, with 10 classifiers
// running at 1.5Ghz, each packet must take less than:
//
// (1.5Ghz * 10 classifiers * (84*8 bits) / 40Gbps) = 252 cycles
//
// The default classifier attempts to spend less than 200 cycles on
// any possible min-packet, to allow customized classifiers to add to
// up 50 cycles per packet, without exceeding this cycle budget.
//
// Note that we thus optimize for "small" packets, and for the "worst"
// possible packet, in various places below.
//
// A customized classifier can recover a few cycles in various ways.
//


// Comment this out to disable "gxio_mpipe_rules_add_dmac()" support.
#define CLASSIFY_DMAC

// Comment this out to disable "gxio_mpipe_rules_add_vlan()" support.
#define CLASSIFY_VLAN

// Define this macro to fix the tcp/udp checksum issue for small ethernet
// frames when padded to certain bytes with non-zero bytes. It's enabled
// by default to increase interoperability. For now it's defined as 90
// according to some feedback from customer site. Comment it out to save
// some resource if the peering device doesn't have such issue.
#define ETH_PADDING_CSUM_FIX_LEN    90

// Define this out to store dmac (plus a 16 bit hash) in custom2,
// and smac (plus a 16 bit hash) in custom3.  This costs 20 cycles.
//--#define CUSTOM_MAC_INFO


// To direct packets for a specific range of UDP and/or TCP ports to
// user space applications, define CUSTOM_UDP_PORTS and/or
// CUSTOM_TCP_PORTS, either with a #define, or better, by compiling
// with: mpipe-cc -D CUSTOM_UDP_PORTS. The port range is empty until
// specified by an mPipe application.
//
// When using a custom UDP or TCP port range, the incoming port within
// the specified range is used to directly compute the bucket. This
// allows mapping packets for a specific port to a specific
// notification ring and thus a specific worker tile.
//

// HACK: Define "SPEWING" to enable some debug spew for the simulator.

#ifndef SPEWING
#define printf(...) /*NOTHING*/
#endif


#define FALSE 0
#define TRUE  1

#define bool int16_t


// The flags below (except FLAG_BE) can be set by the classifier.
// WARNING: Setting FLAG_DP and FLAG_DD simultaneously is forbidden.

#define FLAG_CS 0x01 // compute checksum
#define FLAG_NR 0x02 // use specific notif ring
#define FLAG_DE 0x04 // deliver descriptor and packet
#define FLAG_DD 0x08 // deliver descriptor only
#define FLAG_SQ 0x10 // write general sequence number to gp_sqn
#define FLAG_TS 0x20 // write timestamp to time_stamp_ns/sec
#define FLAG_PS 0x40 // write packet sequence number to packet_sqn
#define FLAG_BE 0x80 // buffer error (set by hardware)



// HACK: Define "TESTING" to build this file with some simple tables
// suitable for (very simple) testing.
//
#ifdef TESTING
#define TESTY(...) __VA_ARGS__
#else
#define TESTY(...) /*nothing*/
#endif

// The mpipe instance id 0: instance 0 or 0x80: instance 1.
// Classifier generates instance bit at upper bit(7) in byte of CTR0.
// This bit will be a mPIPE instance indication to gxio or App. gxio
// used this bit to perform SW buffer return across two mPIPE instances.
uint16_t instance = 0;

// The maximum number of "rules".
// NOTE: This may be customized, but must be between "1" and "31".
#define MAX_RULES 31

// The maximum number of "channels".
#define MAX_CHANNELS 20

// The total number of bytes in each entry of "rule_structs".
#define RULE_STRUCTS_BYTES 16

// For each rule, 256 * headroom + tailroom, then capacity, then
// bucket mask, and then bucket offset, and then, for each standard
// buffer size, a buffer stack.  ISSUE: This must be aligned mod 2,
// and currently this happens only by accident.
uint8_t rule_structs[MAX_RULES * RULE_STRUCTS_BYTES];


// For each possible channel, the legal rules.
uint16_t channel_rules[MAX_CHANNELS * 2] TESTY(= { 0x7FFF, 0xFFFF });


// Next comes two hash tables, implemented as arrays of entries, where
// each entry is a key, and a mask of the rules which allow that key,
// except that the high bit of "rules_hi" indicates a "collision",
// that is, IF the current entry is NOT a match, the next entry must
// be consulted.  Thus, the high bit of "rules_hi" is not available
// for representing an actual rule.
//
// We avoid having to "wrap" while scanning through collisions by
// "duplicating" some initial entries at the end of the array.  These
// entries are known as "overflow" entries.
//
// WARNING: The "empty" slots in the tables must contain some "unused"
// key value, and the "default" rules, with the collision bit clear.
//
// ISSUE: These arrays were "supposed" to be "variable sized" arrays,
// occupying "unused" table memory, but this is not yet supported, so
// for now we allow at most 128 dmac entries and 256 vlan entries,
// plus some room for "overflow" entries.
//
// Since the hypervisor queries the actual size of these arrays, a
// customized classifier can shrink them to get extra "table" space,
// or even disable them entirely.
//


#ifdef CLASSIFY_DMAC

// The maximum number of actual entries in "dmac_table".
// NOTE: This may be customized, but must be at least 2, and a power of 2.
#define CLASSIFY_DMAC_SIZE 128

// The maximum number of overflow entries in "dmac_table".
// NOTE: This may be customized, but must be at least 1.
#define CLASSIFY_DMAC_OVER 16

// The actual number of entries in "dmac_table", minus one.
uint16_t dmac_table_mask TESTY(= 1 - 1);

// The "seed" for the "dmac table".  FIXME: Hmmm.
uint16_t dmac_table_seed;

// The default rules for the "dmac table".
uint16_t dmac_table_rules[2] TESTY(= { 0x7FFF, 0xFFFF });

// Array of entries (dmac01, dmac23, dmac45, rules_hi, rules_lo).
uint16_t dmac_table[(CLASSIFY_DMAC_SIZE + CLASSIFY_DMAC_OVER) * 5]
TESTY(= { 0, 0, 0, 0x7FFF, 0xFFFF, });

#endif


#ifdef CLASSIFY_VLAN

// The maximum number of entries in "vlan_table".
// NOTE: This may be customized, but must be at least 2, and a power of 2.
#define CLASSIFY_VLAN_SIZE 256

// The maximum number of overflow entries in "vlan_table".
// NOTE: This may be customized, but must be at least 1.
#define CLASSIFY_VLAN_OVER 32

// The actual number of entries in "vlan_table", minus one.
uint16_t vlan_table_mask TESTY(= 1 - 1);

// The "seed" for the "vlan table".  FIXME: Hmmm.
uint16_t vlan_table_seed;

// The default rules for the "vlan table".
uint16_t vlan_table_rules[2] TESTY(= { 0x7FFF, 0xFFFF });

// Array of entries (vlan, rules_hi, rules_lo).
uint16_t vlan_table[(CLASSIFY_VLAN_SIZE + CLASSIFY_VLAN_OVER) * 3]
TESTY(= { 0, 0x7FFF, 0xFFFF, });

#endif


#ifdef CUSTOM_TCP_PORTS
// TCP packets whose "dst_port" is in the range in "custom_tcp_ports",
// inclusive, will be treated as having vlan "custom_tcp_vlan".
// NOTE: These may be customized via "gxio_mpipe_classifier_customize()".
uint16_t custom_tcp_ports[2] = { 1, 0 };
uint16_t custom_tcp_vlan = 0xF0F0;
#endif

#ifdef CUSTOM_UDP_PORTS
// UDP packets whose "dst_port" is in the range in "custom_udp_ports",
// inclusive, will be treated as having vlan "custom_udp_vlan".
// NOTE: These may be customized via "gxio_mpipe_classifier_customize()".
uint16_t custom_udp_ports[2] = { 1, 0 };
uint16_t custom_udp_vlan = 0xF1F1;
#endif


// Detect "ethertype" for special Ethernet 802.3 "vlan" packets.
//
// Note that "0x8100" is the standard "ETHERTYPE_VLAN", and 0x88A8 is
// the official Ethernet 802.1ad "Q-in-Q" type, while 0x9100, 0x9200,
// and even 0x9300 appear to be deprecated "Q-in-Q" synonyms.
//
// NOTE: Each of these ethertypes can cost 2 + 2 cycles per packet, so
// a customized classifier can save 8 cycles per packet by deleting
// support for 0x9100 and/or 0x9200.
//
#define ethertype_vlan(E) \
  (((E) == 0x8100) | ((E) == 0x88A8) | ((E) == 0x9100) | ((E) == 0x9200))



void classify() 
{
  // NOTE: This can be non-zero if you have a custom header.
  uint8_t l2_offset = 0;


  // The default "status".
  uint8_t status = 0;

#ifdef CUSTOM_MAC_INFO

  // Skip to "dmac".
  initial_iseek(l2_offset);

  {
    // Prepare to emit mac info.
    initial_oseek(0x18);

    // Get DMAC, and hash it.
    uint16_t dmac01 = checksum_and_hash1_seed2(0, get2());
    uint16_t dmac23 = checksum_and_hash1_acc2(0, get2());
    uint16_t dmac45 = checksum_and_hash1_acc2(0, get2());
    hash1_acc2(0xFFFF);
    hash1_acc2(0xFFFF);

    // Get SMAC, and hash it.
    uint16_t smac01 = checksum_and_hash0_seed2(0, get2());
    uint16_t smac23 = checksum_and_hash0_acc2(0, get2());
    uint16_t smac45 = checksum_and_hash0_acc2(0, get2());
    hash0_acc2(0xFFFF);
    hash0_acc2(0xFFFF);

    // Emit DMAC and part of its hash.
    //--oseek(0x18);
    put2(byteswap(dmac01));
    put2(byteswap(dmac23));
    put2(byteswap(dmac45));
    put2(checksum(hash1_hi(), hash1_lo()));

    // Emit DMAC and part of its hash.
    //--oseek(0x20);
    put2(byteswap(smac01));
    put2(byteswap(smac23));
    put2(byteswap(smac45));
    put2(checksum(hash0_hi(), hash0_lo()));

    // ISSUE: Set a status bit indicating custom2/custom3 are valid?
  }

  // Prepare to emit vlan (below).
  oseek(0x10);

#else

  // Skip to "ethertype".
  initial_iseek(l2_offset + 12);

  // Prepare to emit vlan (below).
  initial_oseek(0x10);

#endif


  // The "packet_size()" of "cut-through" packets should always be at
  // least 4224 bytes, so they will normally be dropped (far below)
  // unless an app sets "capacity" to at least 4224, explicitly, or
  // implicitly, by creating a buffer stack of 10K or 16M buffers.

  // Drop packets which have "errors" ("ME" (mac), "TR" (truncate), or
  // "CE" (crc)).  Apps which may get "cut-through" packets must check
  // these flags again, since they may get set AFTER classification.
  // ISSUE: Some apps will want to handle packets with errors, but it
  // seems likely that some errors will prevent the classifier from
  // being able to properly determine which app should get the packet.
  if (packet_has_error())
    return;


#if defined(CUSTOM_UDP_PORTS_HASH) || defined(CUSTOM_TCP_PORTS_HASH) 
  int16_t custom_bucket_hash = -1;
#endif


  // Acquire the channel.
  uint16_t channel = packet_channel();

#ifdef USE_CTRS
  // Default to basic counters.
  uint16_t ctr0 = 0;
  uint16_t ctr1 = 0;
#endif


  //=== Analyze ethertype. ===//

  // Get "ethertype" (or "length").
  uint16_t ethertype = get2();

  //--printf("Initial ethertype = 0x%04x\n", ethertype);


  // NOTE: Packet 168 in the wireshark sample trace "vlan.cap.gz" contains:
  // [...] 81 00 00 05 00 32 AA AA 03 00 00 0C 01 0B 00 00 00 00 [...]
  // That is, a "vlan" header can wrap an "LLC SNAP" packet.

  // NOTE: We limit vlan header depth to two, to avoid using too many
  // cycles if a min-sized packet is hacked up from nested vlan wrappers.

  uint16_t vlan;

  if (ethertype_vlan(ethertype))
  {
    // NOTE: It can take 15 cycles to get here.

    //--printf("Handling encapsulating ethertype 0x%04x\n", ethertype);

    // Get the "outer vlan".
    vlan = get2();

    // Get the "ethertype".
    ethertype = get2();

    if (ethertype_vlan(ethertype))
    {
      // Skip the "inner vlan".
      uint16_t inner = get2();

      // Get the "ethertype".
      ethertype = get2();

#if 0
      // ISSUE: Triple encapsulation is non-standard.
      if (ethertype_vlan(ethertype))
      {
        // Skip the "really inner vlan".
        uint16_t triple = get2();

        // Get the "ethertype".
        ethertype = get2();
      }
#endif

    }

    // Emit vlan.
    //--oseek(0x10);
    put2(vlan);

    // Mask out the high bits ("priority" and "cfi") of "vlan".
    // ISSUE: Should we do this before emitting "vlan" above?
    vlan = vlan & 0x0FFF;

    // NOTE: It can take 29 cycles to get here.
  }
  else
  {
    // Use an "illegal" vlan.
    vlan = 0xFFFF;

    // Emit vlan.
    //--oseek(0x10);
    put2(vlan);
  }

  printf("vlan = 0x%04x\n", vlan);


  // Normally the "l3" header starts here.
  uint16_t l3_offset = itell();


  // Handle Ethernet 802.2 LLC (Link Layer Control) packets.
  // An "ethertype" from 0x0000 to 0x05dc encodes "payload length".
  // ISSUE: Technically values from 0x05dd to 0x05ff are "undefined".
  if (ethertype < 0x0600)
  {
    // Get DSAP/SSAP.
    uint16_t dsap_ssap = get2();

    switch (dsap_ssap)
    {
#if 0
    case 0x0606:
      // LLC with IP.

      // Skip "Control".
      (void)get1();

      // Reset "l3_offset".
      l3_offset = itell();

      // Pretend the Ethertype is IPv4.
      ethertype = 0x0800;
      break;
#endif

#if 0
    case 0x9898:
      // LLC with ARP.

      // Skip "Control".
      (void)get1();

      // Reset "l3_offset".
      l3_offset = itell();

      // Pretend the Ethertype is ARP.
      ethertype = 0x0806;
      break;
#endif

      // ISSUE: Should we also handle 0xAAAB, 0xABAA, and 0xABAB?
      // NOTE: Packet 168 in the "vlan.cap.gz" file (see above) contains:
      // [...] AA AA 03 00 00 0C 01 0B [...], but since "03 00 00 0C" is
      // a "Cisco" code, and is not "03 00 00 0C", the "01 0B" must NOT
      // be treated as an ethertype (in fact, it is a "PID: PVSTP+").
    case 0xAAAA:
      // SNAP.

      // Handle "Control" of "0x03" and "OUI" of "0x000000".
      // ISSUE: We have never tested this with real packets.
      if (get2() == 0x0300 && get2() == 0x0000)
      {
        // Get "ethertype".
        ethertype = get2();

        // ISSUE: What if "ethertype < 0x0600"?

        // Reset "l3_offset".
        l3_offset = itell();
      }
      else
      {
        // Rewind.
        iseek(l3_offset);
      }

      break;

    default:
      // Unknown.

      // Rewind.
      iseek(l3_offset);
      break;
    }
  }

  printf("ethertype = 0x%04x\n", ethertype);

  // Emit ethertype.
  //--oseek(0x12);
  put2(ethertype);

  // Emit l2 start.
  //--oseek(0x14);
  put1(l2_offset);

  // Emit l3 start.
  //--oseek(0x15);
  put1(l3_offset);

  // NOTE: It can take 29 + 20 cycles to get "here" (including the
  // hash and jump for the switch statement below).


  //=== Handle IPv4 and IPv6 and other packets. ===//

  uint16_t protocol = 0;

  // The "flags".  NOTE: May get "FLAG_CS" added below.
  // NOTE: The "FLAG_SQ" matches the "gp_sqn_sel" far below.
  uint16_t flags = FLAG_DE | FLAG_SQ | FLAG_TS | FLAG_PS;

  // Checksum of the src+dst IP addresses, used for L4 pseudoheader.
  uint16_t addr_checksum = 0;

  // Used only for IPv4/IPv6 TCP/UDP checksum computation.
  uint16_t l4_length;

  // Offset of the L4 header
  uint16_t l4_offset;

  // ISSUE: Annoyingly, "0x0800" and "0x08dd" hash to the same jump
  // target when given 8 possible jump targets, so we end up using 32
  // jump targets (64 bytes) for the following switch statement.  We
  // could thus add one permute instruction to save 48 bytes of data.

  // ISSUE: This "switch" statement takes 1 + 1 + 2 cycles to hash and
  // jump, plus another 1 + 1 cycles to verify IPv4 or IPv6.  A simple
  // "if" idiom would take 1 + 1 cycles for IPv4, 1 + 1 + 2 + 1 + 1
  // cycles for IPv6, or 1 + 1 + 2 + 1 + 1 + 2 for "other", and would
  // actually save 2 instructions, and 64 switch table bytes, and 2
  // cycles for IPv4, at the cost of 4 cycles for "other".

  switch (ethertype)
  {
  case 0x0800: 
    // IPv4.
    {
      // "Version/IHL" and "Differentiated Services".
      uint16_t ver_ihl_ds = get2();

#ifdef DEBUG
      uint16_t ip_version = ver_ihl_ds >> 12;
      if (ip_version != 4)
        return;
#endif

      int16_t header_length = (ver_ihl_ds >> 8) & 0xF;

#ifdef DEBUG
      if (header_length < 5)
        return;
#endif

      // Read in the remaining nine words of the header.
      uint16_t ip_length = get2();
      uint16_t identification = get2();
      uint16_t flags_and_fragment_offset = get2();
      uint16_t ttl_and_protocol = get2();
      uint16_t expected_checksum = get2();

      // Extract the protocol byte.
      protocol = ttl_and_protocol & 0xFF;

      // Compute the header checksum, including the expected checksum field.
      uint16_t header_checksum = checksum(ver_ihl_ds, ip_length);
      header_checksum = checksum(header_checksum, identification);
      header_checksum = checksum(header_checksum, flags_and_fragment_offset);
      header_checksum = checksum(header_checksum, ttl_and_protocol);
      header_checksum = checksum(header_checksum, expected_checksum);

      // Checksum and hash the src/dst IP addresses.  We do them
      // separately because they are common between IP and TCP checksums.
      addr_checksum = checksum_and_hash0_seed2(0, get2());
      addr_checksum = checksum_and_hash0_acc2(addr_checksum, get2());
      addr_checksum = checksum_and_hash1_seed2(addr_checksum, get2());
      addr_checksum = checksum_and_hash1_acc2(addr_checksum, get2());

      header_checksum = checksum(header_checksum, addr_checksum);

      // FIXME: Handle tunneling protocols?

      // Detect fragmented packets.  From left to right, bit 0 is
      // "always zero", bit 1 is "do NOT fragment", bit 2 is "more
      // fragments follow", and bits 3 - 15 are the "fragment offset".
      // So we mask out bit 1 ("1 << 14") and check for non-zero.
      if ((flags_and_fragment_offset & ~(1 << 14)) != 0)
      {
        printf("Detected fragmented packet with protocol 0x%x.\n", protocol);

        // HACK: Fragmented packets are not "real" TCP/UDP packets.
        // ISSUE: Set a "fragment" bit in "status"?
        protocol = 0;
      }

      // Prepare to checksum.
      uint16_t ip_header_length_in_bytes = header_length << 2;
      l4_length = ip_length - ip_header_length_in_bytes;

      // Checksum IP options, if any.
      for (int16_t i = header_length - 6; i >= 0; i--)
      {
        header_checksum = checksum(header_checksum, get2());
        header_checksum = checksum(header_checksum, get2());
      }

      // Do "status |= 0x80" if header checksum is bad.
      // NOTE: This compiles to a "compare", plus a "left shift by 7",
      // and the latter is combined with the "put1()" below.
      status |= (header_checksum != 0xFFFF) * 0x80;

      // Emit l4 offset.
      //--oseek(0x16);
      l4_offset = itell();
      put1(l4_offset);

      // Emit status.
      //--oseek(0x17);
      put1(status);

      // NOTE: It can take 49 + 20 + 50 (5 per option) + 11 cycles to get
      // here (including the "mseek(channel_rules[channel * 2 + 0]" below).
      // This is represented as "80 + N*5" cycles below, and the "N*5" can
      // be assumed to be zero, since a 60 byte min-packet does not have
      // room for Q-in-Q plus SNAP plus IPv4 options plus IPv4 TCP/UDP.
    }
    break;

  case 0x86dd:
    // IPv6.
    {
      uint16_t ver_tc_fl = get2();

#ifdef DEBUG
      uint16_t ip_version = ver_tc_fl >> 12;
      if (ip_version != 6)
        return;
#endif

      // Skip flow label.
      (void)get2();

      l4_length = get2();

      protocol = get2() >> 8;

      // Checksum and hash the src address.
      addr_checksum = checksum_and_hash0_seed2(0, get2());
      addr_checksum = checksum_and_hash0_acc2(addr_checksum, get2());
      addr_checksum = checksum_and_hash0_acc2(addr_checksum, get2());
      addr_checksum = checksum_and_hash0_acc2(addr_checksum, get2());
      addr_checksum = checksum_and_hash0_acc2(addr_checksum, get2());
      addr_checksum = checksum_and_hash0_acc2(addr_checksum, get2());
      addr_checksum = checksum_and_hash0_acc2(addr_checksum, get2());
      addr_checksum = checksum_and_hash0_acc2(addr_checksum, get2());

      // Checksum and hash the dst address.
      addr_checksum = checksum_and_hash1_seed2(addr_checksum, get2());
      addr_checksum = checksum_and_hash1_acc2(addr_checksum, get2());
      addr_checksum = checksum_and_hash1_acc2(addr_checksum, get2());
      addr_checksum = checksum_and_hash1_acc2(addr_checksum, get2());
      addr_checksum = checksum_and_hash1_acc2(addr_checksum, get2());
      addr_checksum = checksum_and_hash1_acc2(addr_checksum, get2());
      addr_checksum = checksum_and_hash1_acc2(addr_checksum, get2());
      addr_checksum = checksum_and_hash1_acc2(addr_checksum, get2());

      // Emit l4 offset.
      //--oseek(0x16);
      l4_offset = itell();
      put1(l4_offset);

      // Emit status.
      //--oseek(0x17);
      put1(status);

      // NOTE: It can take 78 (49 + 29) cycles to get here
      // (including the "mseek(channel_rules[channel * 2 + 0]" below).
      // This is smaller than the IPv4 cost, and besides, IPv6 packets
      // are BIGGER than IPv4 packets, and thus get more cycles.
    }
    break;

  default:
    {
#ifndef CUSTOM_MAC_INFO

      // NOTE: The "(is_tcp | is_udp)" code below will NOT fire, and thus,
      // none of its "get2()" calls will be confused by this seek.
      iseek(l2_offset);

      // Get DMAC, and hash it.
      uint16_t dmac01 = checksum_and_hash1_seed2(0, get2());
      uint16_t dmac23 = checksum_and_hash1_acc2(0, get2());
      uint16_t dmac45 = checksum_and_hash1_acc2(0, get2());
      hash1_acc2(0xFFFF);
      hash1_acc2(0xFFFF);

      // Get SMAC, and hash it.
      uint16_t smac01 = checksum_and_hash0_seed2(0, get2());
      uint16_t smac23 = checksum_and_hash0_acc2(0, get2());
      uint16_t smac45 = checksum_and_hash0_acc2(0, get2());
      hash0_acc2(0xFFFF);
      hash0_acc2(0xFFFF);

#endif

      // There is no l4 offset.
      //--oseek(0x16);
      put1(0);

      // Emit status.
      //--oseek(0x17);
      put1(status);
    }
    break;
  }

tcp_udp_check:; // semicolon needed to avoid compile issue with the x86 wrapper
  bool is_tcp = (protocol == 0x06);
  bool is_udp = (protocol == 0x11);

  if (is_tcp | is_udp)
  {
    printf("Handling TCP/UDP\n");

    // Emit csum_start and csum_seed (ignored without FLAG_CS).

    oseek(0x09);

    // The L4 checksum starts at the ports.
    uint16_t csum_start = itell();
    put1(csum_start);

    // The L4 checksum includes a pseudo-header.
    uint16_t csum_seed = checksum(addr_checksum, protocol);
    csum_seed = checksum(csum_seed, l4_length);

#ifdef ETH_PADDING_CSUM_FIX_LEN
    // This block is to workaround the checksum issue for small
    // ethernet frames padded to certain bytes (not including the FCS).
    uint16_t pkt_len = packet_size(), csum_adjust = 0;
    if (pkt_len < ETH_PADDING_CSUM_FIX_LEN)
    {
      uint16_t end = l4_offset + l4_length;
      // Check if padding exists.
      if (end < pkt_len)
      {
        // Start from even address.
        if (end & 1)
        {
          iseek(end - 1);
          csum_adjust = get2() & 0x00FF;
          csum_adjust = checksum(0, csum_adjust);
          end += 2;
        }
        else
          iseek(end);

        // Adjust csum_seed every two bytes.
        // When frame is padded, it's reasonable to assume that pkt_len
        // is an even number. So no further checking is added after the
        // loop.
        for (; end < pkt_len; end += 2)
          csum_adjust = checksum(csum_adjust, get2());

        csum_adjust = 0xFFFF - csum_adjust;
        csum_seed = checksum(csum_seed, csum_adjust);

        // Restore the position.
        iseek(csum_start);
      }
    }
#endif

    put2(csum_seed);

    // Get the src/dst ports.
    uint16_t src_port = get2();
    uint16_t dst_port = get2();

    // Hash the ports.
    hash0_acc2(src_port);
    hash1_acc2(dst_port);

    if (is_tcp)
    {
#ifdef CUSTOM_TCP_PORTS
      if (custom_tcp_ports[0] <= dst_port && 
          dst_port <= custom_tcp_ports[1])
      {
        vlan = custom_tcp_vlan;
#ifdef CUSTOM_TCP_PORTS_HASH
        // Directly map the custom port to a specific bucket by
        // computing the bucket_hash value here. The bucket is the
        // difference between the current port and the first port in
        // the custom range.
        custom_bucket_hash = dst_port - custom_tcp_ports[0];
#endif
      }
#endif
    }
    else
    {
#ifdef CUSTOM_UDP_PORTS
      if (custom_udp_ports[0] <= dst_port && 
          dst_port <= custom_udp_ports[1])
      {
        vlan = custom_udp_vlan;
#ifdef CUSTOM_UDP_PORTS_HASH
        // Directly map the custom port to a specific bucket by
        // computing the bucket_hash value here. The bucket is the
        // difference between the current port and the first port in
        // the custom range.
        custom_bucket_hash = dst_port - custom_udp_ports[0];
#endif
      }
#endif
    }

    // NOTE: The "udp_csum" and "skip_csum_if_udp" variables are only
    // "valid" if "is_udp", but if "is_tcp", then they will definitely
    // be contained in the header of any well-formed packet, and their
    // actual values will be irrelevent to the logic below.  Note that
    // we avoid branching via careful bit arithmetic.

    // Skip the length (only valid if UDP).
    (void)get2();

    // Get the UDP checksum (only valid if UDP).
    uint16_t udp_csum_if_udp = get2();

    // NOTE: If the UDP checksum is zero, there is no checksum.
    bool need_csum_if_udp = (udp_csum_if_udp != 0);

    // Set the "csum" flag if necessary.
    // NOTE: Since "FLAG_CS" is 1, multiplying by it is free.
    flags |= ((is_tcp | need_csum_if_udp) * FLAG_CS);

    //--oseek(0x0C);
  }
  else
  {
    //
    // Further parsing if not TCP/UDP.
    //
    if (ethertype == 0x0800)   // IPV4
    {
        switch (protocol)
        {
          case 51:      // AH (RFC 2402), Checksum and hash the SPI
            (void)get2();
            (void)get2();

            // fall through
          case 50:      // ESP (RFC 2406), Checksum and hash the SPI
            addr_checksum = checksum_and_hash0_seed2(0, get2());
            addr_checksum = checksum_and_hash0_acc2(addr_checksum, get2());
            break;

          default:
            break;
        }
    }
    else if (ethertype == 0x86dd)    // IPv6 (RFC 2460)
    {
      uint8_t hdr_ext_len, is_fragment = 0;
      uint16_t protocol_and_len, total_ext_len = 0;
      for (;;)  // Parsing the extension headers
      {
        switch (protocol)
        {
          case 0:       // Hop-by-Hop Options Header
          case 43:      // Routing Header
          case 60:      // Destination Options Header
          // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
          // |  Next Header  |  Hdr Ext Len  |                               |
          // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
          // .                         ...                                   .
          // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            protocol_and_len = get2();
            protocol = protocol_and_len >> 8;
            hdr_ext_len = protocol_and_len & 0xFF;
            (void)get2();
            (void)get2();
            (void)get2();
            break;

          case 44:      // Fragment Header
          // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
          // |  Next Header  |   Reserved    |      Fragment Offset    |Res|M|
          // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
          // |                         Identification                        |
          // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            is_fragment = 1;
            protocol_and_len = get2();
            protocol = protocol_and_len >> 8;
            hdr_ext_len = 0;
            (void)get2();
            (void)get2();
            (void)get2();
            break;

          case 6:       // TCP, jump back to the common tcp/udp processing
          case 17:      // UDP, jump back to the common tcp/udp processing
            if (is_fragment)        // Don't continue if fragments
                goto v6_parsing_done;
            // Update l4 offset and length
            oseek(0x16);
            l4_offset += total_ext_len;
            put1(l4_offset);
            l4_length -= total_ext_len;
            goto tcp_udp_check;

          case 51:      // AH (RFC 2402), Checksum and hash the SPI
          // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
          // | Next Header   |  Payload Len  |          RESERVED             |
          // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
          // |                 Security Parameters Index (SPI)               |
          // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
          // |                       ...                                     |
          // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            (void)get2();
            (void)get2();
            // fall through

          case 50:      // ESP (RFC 2406), Checksum and hash the SPI
          // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
          // |               Security Parameters Index (SPI)                 |
          // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
          // |                       ...                                     |
          // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            if (is_fragment)    // Don't continue if fragments
                goto v6_parsing_done;
            addr_checksum = checksum_and_hash0_seed2(0, get2());
            addr_checksum = checksum_and_hash0_acc2(addr_checksum, get2());
            // fall through

          default:      // Default done
            goto v6_parsing_done;
        }

        // Protect against attacking. 
        total_ext_len += ((hdr_ext_len + 1) << 3);
        if (total_ext_len > 192)
            return;

        // Skip the header in 8-octets
        while (hdr_ext_len > 0)
        {
          hdr_ext_len--;
          (void)get2();
          (void)get2();
          (void)get2();
          (void)get2();
        }
      }

v6_parsing_done:
      // Update l4 offset
      oseek(0x16);
      l4_offset += total_ext_len;
      put1(l4_offset);
    }

    oseek(0x0C);
  }

  // NOTE: It can take up to 80 + N*5 + 14 cycles to get here.


  //=== Finish hashing. ===//

  // Spread the bits around some more.
  hash0_acc2(0xFFFF);
  hash0_acc2(0xFFFF);
  hash1_acc2(0xFFFF);
  hash1_acc2(0xFFFF);

  // Compute a commutative hash.
  uint16_t hash_lo = checksum(hash0_lo(), hash1_lo());
  uint16_t hash_hi = checksum(hash0_hi(), hash1_hi());

  // And include the protocol.
  hash_lo = checksum(hash_lo, protocol);

  //--oseek(0x0C);

  // Emit the actual flow hash.
  put2(hash_lo);
  put2(hash_hi);

  // The bucket hash (used below) comes from the flow hash.
  // ISSUE: We could presumably just use half of the flow hash.
  uint16_t bucket_hash = checksum(hash_lo, hash_hi);

#if defined(CUSTOM_UDP_PORTS_HASH) || defined(CUSTOM_TCP_PORTS_HASH) 
  // See above
  if (custom_bucket_hash >= 0)
    bucket_hash = custom_bucket_hash;
#endif

  //=== Restrict "legal_rules" by "channel". ===//

  // Start with the maximal set of legal rules for "channel".
  mseek(&channel_rules[channel * 2 + 0]);
  uint16_t legal_rules_hi = mem2();
  uint16_t legal_rules_lo = mem2();


#ifdef CLASSIFY_DMAC

  //=== Restrict "legal_rules" by "dmac". ===//

  // Consulting the dmac_table costs 10 cycles of setup, plus 3 stall
  // cycles, plus 10 cycles per hash table entry consulted, plus
  // another 3 cycles if no entry actually matches.  With a "decent"
  // table, this should be limited to 10+3+10+3 = 26 cycles.

  // Get "dmac", and hash it.
  iseek(l2_offset);
  hash0_seed2(dmac_table_seed);

  uint16_t dmac01 = checksum_and_hash0_acc2(0, get2());
  uint16_t dmac23 = checksum_and_hash0_acc2(0, get2());
  uint16_t dmac45 = checksum_and_hash0_acc2(0, get2());
  uint16_t dmac_table_hash = checksum(hash0_lo(), hash0_hi());

#if 0
  printf("Initial legal rules = 0x%x 0x%x.\n",
         legal_rules_hi, legal_rules_lo);

  printf("dmac = 0x%04x 0x%04x 0x%04x hash = 0x%04x\n",
         dmac01, dmac23, dmac45, dmac_table_hash);
#endif

  // Start on the appropriate entry in the hash table.  Note that the
  // mask only has to be applied during the initial hash, because the
  // table is "unrolled" using "overflow" entries.
  mseek(&dmac_table[(dmac_table_hash & dmac_table_mask) * (3 + 2)]);

  // NOTE: It can take up to 80 + N*5 + 14 + 18 cycles to get here.
  // ISSUE: We stall for three cycles here (but NOT inside the loop).

  while (1)
  {
    // Each entry in this hash table consists of other01, other23,
    // other45, rules_hi, and rules_lo, but we "inline" the reading of
    // rules_hi and rules_lo below to allow efficient compilation.

    uint16_t other01 = mem2();
    uint16_t other23 = mem2();
    uint16_t other45 = mem2();

    // ISSUE: Using "&&" here crashes "tile-mpipe-cc".
    bool eq = (other01 == dmac01) & (other23 == dmac23) & (other45 == dmac45);

#if 0
    printf("Comparing dmac 0x%04x 0x%04x 0x%04x to 0x%04x 0x%04x 0x%04x.\n",
           dmac01, dmac23, dmac45, other01, other23, other45);
#endif

    if (!eq)
    {
      // Handle non-matches.
      // NOTE: Compiled as "likely".

      uint16_t rules_hi = mem2();

      if (rules_hi & 0x8000)
      {
        // Handle conflicts.
        // NOTE: Compiled as "likely", yielding 6+1+2+1 cycles per entry.

        uint16_t rules_lo = mem2();
        continue;
      }

      // Handle failures.
      // NOTE: Compiled as "unlikely" yielding 6+1+2+3+1 cycles per entry.

      legal_rules_hi &= dmac_table_rules[0];
      legal_rules_lo &= dmac_table_rules[1];

      printf("Matched no dmac, legal rules = 0x%x 0x%x.\n",
             legal_rules_hi, legal_rules_lo);

      break;
    }

    // Handle success.
    // NOTE: Compiled as "unlikely" yielding 6+2+2 cycles per entry.

    uint16_t rules_hi = mem2();
    uint16_t rules_lo = mem2();

    legal_rules_hi &= rules_hi;
    legal_rules_lo &= rules_lo;

    printf("Matched a dmac, legal rules = 0x%x 0x%x.\n",
           legal_rules_hi, legal_rules_lo);

    break;
  }

#endif

#ifdef CLASSIFY_VLAN

  //=== Restrict "legal_rules" by "vlan". ===//

  // Consulting the vlan_table costs 6 cycles of setup, plus 4 stall
  // cycles, plus 6 cycles per hash table entry consulted, plus
  // another 3 cycles if an entry actually matches.  With a "decent"
  // table, this should be limited to 6+4+6+3 = 19 cycles.

  // Hash "vlan".
  // ISSUE: Is this excessive?
  hash0_seed2(vlan_table_seed);
  hash0_acc2(vlan);

  // ISSUE: We stall for a cycle here.
  uint16_t vlan_table_hash = checksum(hash0_lo(), hash0_hi());

  // Start on the appropriate entry in the hash table.  Note that the
  // mask only has to be applied during the initial hash, because the
  // table is "unrolled" using "overflow" entries.
  mseek(&vlan_table[(vlan_table_hash & vlan_table_mask) * (1 + 2)]);

  // ISSUE: We stall for three cycles here (but NOT inside the loop).

  while (1)
  {
    // Each entry in this hash table consists of other, rules_hi, and
    // rules_lo, but we "inline" the reading of rules_hi and rules_lo
    // below to allow efficient compilation.

    uint16_t other = mem2();

    bool eq = (other == vlan);

#if 0
    printf("Comparing vlan 0x%04x to 0x%04x.\n", vlan, other);
#endif

    if (!eq)
    {
      // Handle non-matches.
      // NOTE: Compiled as "likely".

      uint16_t rules_hi = mem2();

      if (rules_hi & 0x8000)
      {
        // Handle conflicts.
        // NOTE: Compiled as "likely", yielding 2+1+2+1 cycles per entry.

        uint16_t rules_lo = mem2();
        continue;
      }

      // Handle failures.
      // NOTE: Compiled as "unlikely", yielding 2+1+2+3+1 cycles per entry.

      legal_rules_hi &= vlan_table_rules[0];
      legal_rules_lo &= vlan_table_rules[1];

      printf("Matched no vlan, legal rules = 0x%x 0x%x.\n",
             legal_rules_hi, legal_rules_lo);

      break;
    }

    // Handle success.
    // NOTE: Compiled as "unlikely", yielding 2+2+2 cycles per entry.

    uint16_t rules_hi = mem2();
    uint16_t rules_lo = mem2();

    legal_rules_hi &= rules_hi;
    legal_rules_lo &= rules_lo;

    printf("Matched a vlan, legal rules = 0x%x 0x%x.\n",
           legal_rules_hi, legal_rules_lo);

    break;
  }

  // NOTE: Assuming sparse hash tables (with no conflict entries),
  // it could take 80 + N*5 + 14 + 21 + 11 + 16 cycles to get here.

#endif


  //=== Extract a "rule" from "legal_rules". ===//

  // Note that "channel_rules" must not contain any values with the
  // high bit set, so "legal_rules_hi" will never have the high bit set.

  uint16_t rule;

  // This code takes 5 cycles.  It relies on the facts that "ctz(0) == 16"
  // and "ctz(xxx | 1) == 0", so the expression below is equivalent to:
  // "(legal_rules_lo == 0) ? ctz(legal_rules_lo) : ctz(legal_rules_hi) + 16".
  rule = ctz(legal_rules_lo) + ctz(legal_rules_hi | (legal_rules_lo != 0));

  // Seek to the "info" for the given rule.  NOTE: We do this before
  // checking if "rule" is valid, to help the compiler avoid stalls.
  mseek(&rule_structs[rule * RULE_STRUCTS_BYTES]);

  // Drop packet if "(legal_rules_lo | legal_rules_hi) == 0", in which
  // case, "rule" will be "32".
  if (rule >= MAX_RULES)
    return;

  printf("Acquired rule %d from rule bits 0x%x and 0x%x\n",
         rule, legal_rules_hi, legal_rules_lo);

  // NOTE: It could take 142 + 9 cycles to get here.


  //=== Compute needed, apply capacity, emit headroom. ===//

  // Acquire headroom and tailroom.
  uint8_t headroom = mem1();
  uint8_t tailroom = mem1();

  // Emit headroom (aka "Offset").
  // NOTE: Doing this up here helps the compiler avoid stalls.
  oseek(0x38);
  put1(headroom);

  // Compute the number of bytes actually needed by the packet.
  uint16_t needed = packet_size() + headroom + tailroom;

  // Drop the packet if it is "too big".
  uint16_t capacity = mem2();
  if (needed > capacity)
    return;

  // NOTE: It could take 142 + 9 + 8 cycles to get here.


  //=== Emit bucket/flags/counters. ===//

  oseek(0x04);

  // Pick a bucket id.
  uint16_t bucket_mask = mem2();
  uint16_t bucket_first = mem2();
  uint16_t bucket = bucket_first + (bucket_hash & bucket_mask);

  // Emit bucket id.
  put2(bucket);

  // Emit flags.
  put1(flags);

  // NOTE: It could take 142 + 9 + 8 + 8 cycles to get here.

  // WARNING: From here, calling "return" will NOT drop the packet.

#ifdef USE_CTRS
  // Emit ctr0, ctr1 and instance bit. 
  put1(ctr0 | instance);
  put1(ctr1);
  
#else
  // Emit instance bit.
  put1(instance);
#endif


  //=== Emit gp_sqn_sel ===//

  oseek(0x28);

  // Use the "bucket" to select a counter for "gp_sqn".
  // NOTE: Other interesting values would include "bucket_first"
  // (indicates the "rule") or "channel" (indicates the "link").
  uint16_t gp_sqn_sel = bucket;

  put2(gp_sqn_sel);


  //=== Emit buffer stack index. ===//

  //--mseek(&rule_structs[rule * 16 + 8]);

  // Now, we must determines the proper "stack", and emit it at offset
  // 0x3E.  The smaller the packet, the fewer cycles we would like to
  // use, which allows for some interesting optimizations involving
  // optimistically emitting a stack, and then possibly changing it.
  // The instruction and cycle counts below include all jumps to zero.

  // Optimistically assume needed <= 128.
  oseek(0x3E);
  put1(mem1());
  if (needed > 128)
  {

#if 1

    // Instructions: 31.  Total cycles: 6, 10, 14, 18, 22, 26, 30, 32
    // (4+2, 4+4+2, 4+4+4+2, ..., 4+4+4+4+4+4+4+2, 4+4+4+4+4+4+4+3+1).

    // Optimistically assume needed <= 256.
    oseek(0x3E);
    put1(mem1());
    if (needed > 256)
    {
      // Optimistically assume needed <= 512.
      oseek(0x3E);
      put1(mem1());
      if (needed > 512)
      {
        // Optimistically assume needed <= 1024.
        oseek(0x3E);
        put1(mem1());
        if (needed > 1024)
        {
          // Optimistically assume needed <= 1664.
          oseek(0x3E);
          put1(mem1());
          if (needed > 1664)
          {
            // Optimistically assume needed <= 4096.
            oseek(0x3E);
            put1(mem1());
            if (needed > 4096)
            {
              // Optimistically assume needed <= 10368.
              oseek(0x3E);
              put1(mem1());
              if (needed > 10368)
              {
                oseek(0x3E);
                put1(mem1());
              }
            }
          }
        }
      }
    }

#else

    // Instructions: 21.  Total cycles: 6, 24, 24, 24, 24, 24, 24, 24
    // (4+2, 4+16+2+1+1, ...)

    uint16_t size = 15;
    size -= (needed <= 256);
    size -= (needed <= 512);
    size -= (needed <= 1024);
    size -= (needed <= 1664);
    size -= (needed <= 4096);
    size -= (needed <= 10368);
    uint8_t stack = rule_structs[rule * RULE_STRUCTS_BYTES + size];
    oseek(0x3E);
    put1(stack);

#endif

  }

  // NOTE: A small packet could take 171 cycles to get here.
}
