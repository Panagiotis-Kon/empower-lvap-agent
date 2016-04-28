/*
 * transmissionpolicies.{cc,hh} -- Transmission Policies
 * Roberto Riggio
 *
 * Copyright (c) 2016 CREATE-NET
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/straccum.hh>
#include <clicknet/ether.h>
#include "transmissionpolicies.hh"
CLICK_DECLS

TransmissionPolicies::TransmissionPolicies() : _default_tx_policy(0) {
}

TransmissionPolicies::~TransmissionPolicies() {
}

int TransmissionPolicies::configure(Vector<String> &conf, ErrorHandler *errh) {

	int res = 0;

	for (int x = 0; x < conf.size(); x++) {

		Vector<String> args;
		cp_spacevec(conf[x], args);

		if (args.size() != 2) {
			return errh->error("error param %s must have 2 args", conf[x].c_str());
		}

		if (args[0] == "DEFAULT") {

			TransmissionPolicy * tx_policy;

			if (!ElementCastArg("TransmissionPolicy").parse(args[1], tx_policy, Args(conf, this, errh))) {
				return errh->error("error param %s: must be a TransmissionPolicy element", conf[x].c_str());
			}

			_default_tx_policy = tx_policy->tx_policy();

		} else {

			EtherAddress eth;

			if (!EtherAddressArg().parse(args[0], eth)) {
				return errh->error("error param %s: must start with ethernet address", conf[x].c_str());
			}

			TransmissionPolicy * tx_policy;

			if (!ElementCastArg("TransmissionPolicy").parse(args[1], tx_policy, Args(conf, this, errh))) {
				return errh->error("error param %s: must be a TransmissionPolicy element", conf[x].c_str());
			}

			_tx_table.insert(eth, tx_policy->tx_policy());

		}

	}

	return res;

}

TxPolicyInfo *
TransmissionPolicies::lookup(EtherAddress eth) {

	if (!eth) {
		click_chatter("%s: lookup called with NULL eth!\n", name().c_str());
		return new TxPolicyInfo();
	}

	TxPolicyInfo * dst = _tx_table.find(eth);

	if (dst) {
		return dst;
	}

	if (_default_tx_policy) {
		return _default_tx_policy;
	}

	return new TxPolicyInfo();

}

TxPolicyInfo *
TransmissionPolicies::supported(EtherAddress eth) {

	if (!eth) {
		click_chatter("%s: lookup called with NULL eth!\n", name().c_str());
		return new TxPolicyInfo();
	}

	TxPolicyInfo *dst = _tx_table.find(eth);

	if (dst) {
		return dst;
	}

	return new TxPolicyInfo();

}

int TransmissionPolicies::insert(EtherAddress eth, Vector<int> mcs) {
	return insert(eth, mcs, false, TP_BR, 2436);
}

int TransmissionPolicies::insert(EtherAddress eth, Vector<int> mcs, bool no_ack, tp_mcs_selec_types mcs_select, int rts_cts) {

	if (!(eth)) {
		click_chatter("TransmissionPolicies %s: You fool, you tried to insert %s\n",
					  name().c_str(),
					  eth.unparse().c_str());
		return -1;
	}

	TxPolicyInfo *dst = _tx_table.find(eth);

	if (!dst) {
		_tx_table.insert(eth, new TxPolicyInfo());
		dst = _tx_table.find(eth);
	}

	dst->_mcs.clear();
	dst->_no_ack = no_ack;
	dst->_mcs_select = mcs_select;
	dst->_rts_cts = rts_cts;

	if (_default_tx_policy->_mcs.size()) {
		/* only add rates that are in the default rates */
		for (int x = 0; x < mcs.size(); x++) {
			for (int y = 0; y < _default_tx_policy->_mcs.size(); y++) {
				if (mcs[x] == _default_tx_policy->_mcs[y]) {
					dst->_mcs.push_back(mcs[x]);
				}
			}
		}
	} else {
		dst->_mcs = mcs;
	}

	return 0;

}

int TransmissionPolicies::remove(EtherAddress eth) {

	if (!(eth)) {
		click_chatter("TransmissionPolicies %s: You fool, you tried to insert %s\n",
					  name().c_str(),
					  eth.unparse().c_str());
		return -1;
	}

	TxPolicyInfo *dst = _tx_table.find(eth);

	if (!dst) {
		return -1;
	}

	_tx_table.remove(eth);

	return 0;

}

enum {
	H_INSERT, H_REMOVE, H_POLICIES
};

String TransmissionPolicies::read_handler(Element *e, void *thunk) {
	TransmissionPolicies *td = (TransmissionPolicies *) e;
	switch ((uintptr_t) thunk) {
	case H_POLICIES: {
	    StringAccum sa;
	    sa << "DEFAULT " << td->default_tx_policy()->unparse() << "\n";
		for (TxTableIter it = td->tx_table()->begin(); it.live(); it++) {
		    sa << it.key().unparse() << " " << it.value()->unparse() << "\n";
		}
		return sa.take_string();
	}
	default:
		return String();
	}
}

int TransmissionPolicies::write_handler(const String &in_s, Element *e,
		void *vparam, ErrorHandler *errh) {

	TransmissionPolicies *f = (TransmissionPolicies *) e;
	String s = cp_uncomment(in_s);

	switch ((intptr_t) vparam) {
	case H_INSERT: {

		Vector<String> tokens;
		cp_spacevec(s, tokens);

		if (tokens.size() < 2)
			return errh->error("insert requires at least 2 parameters");

		EtherAddress dst;
		bool no_ack = false;
		int rts_cts = 2436;
		String mcs_string;
		tp_mcs_selec_types mcs_select = TP_BR;
		Vector<int> mcs;

		if (!EtherAddressArg().parse(tokens[0], dst)) {
			return errh->error("error param %s: must start with an Ethernet address", s.c_str());
		}

		for (int x = 1; x < tokens.size(); x++) {
			int r = 0;
			IntArg().parse(tokens[x], r);
			mcs.push_back(r);
		}

		f->insert(dst, mcs, no_ack, mcs_select, rts_cts);

		break;

	}
	case H_REMOVE: {

		EtherAddress dst;

		if (!EtherAddressArg().parse(s, dst)) {
			return errh->error("error param %s: must start with an Ethernet address", s.c_str());
		}

		f->remove(dst);

		break;

	}
	}
	return 0;
}

void TransmissionPolicies::add_handlers() {
	add_read_handler("policies", read_handler, (void *) H_POLICIES);
	add_write_handler("insert", write_handler, (void *) H_INSERT);
	add_write_handler("remove", write_handler, (void *) H_REMOVE);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TransmissionPolicies)

