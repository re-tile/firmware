#!/usr/bin/env python
#
# gen_events: Generate oprofile events file from XML performance counter
# description

import diag  # diag.py is imported from directory set by PYTHONPATH
import sys
import time

# min_count -- returns min_count value for specified event shortname.
# Most events default to an arbitrary min_count value of 500,
# with a few special-case exceptions, such as PASS/FAIL/DONE.
def min_count(event_name):
    result = 500
    if event_name == 'PASS_WRITTEN':
      result = 0
    elif event_name == 'FAIL_WRITTEN':
      result = 0
    elif event_name == 'DONE_WRITTEN':
      result = 0
    return result

def main(argv):
    chip = argv[1]
    xml_files = argv[2:]

    (all_sections, ignore) = diag.parse_diag_xml_files(xml_files)

    if chip == '0':
        print "# TILE64 Events"
        counter_str = "counters:0,1"
    elif chip == '1':
        print "# TILEPro Events"
        counter_str = "counters:0,1,2,3"
    elif chip == '10':
        print "# TILE-Gx Events"
        counter_str = "counters:0,1,2,3"
    elif chip == '11':
        print "# TILE-Hx Events"
        counter_str = "counters:0,1,2,3"
    else:
        print "Unknown Chip Type"
    print
    if chip >= '10':
        for section in all_sections:
            for event in section.events:
                if event.status == "oprofile":
                    if section.shortname == 'MBOX':
                        count_box = 1
                    elif section.shortname == 'CBOX':
                        count_box = 2
                    elif section.shortname == 'SBOX':
                        count_box = 3
                    elif section.shortname == 'NETWORK':
                        count_box = 4
                    elif section.shortname == 'BCST':
                        count_box = 5
                    elif section.shortname == 'TOP':
                        count_box = 6
                    elif section.shortname == 'SPCL':
                        count_box = 7
                    else:
                        count_box = 0
                    event_value = count_box << 6
                    event_value += event.vec_bit
                    print "event:%#04x %s um:zero minimum:%d name:%s : %s" % \
                          (event_value, counter_str,
			   min_count(event.shortname),
			   event.shortname, event.description)
    else:
        for section in all_sections:
            for event in section.events:
                if event.status == "oprofile":
                    print "event:%#04x %s um:zero minimum:%d name:%s : %s" % \
                          (event.global_vec_bit, counter_str,
			   min_count(event.shortname),
			   event.shortname, event.description)

if __name__ == "__main__":
    main(sys.argv)
