/*
 * LogSystemConfig.h
 *
 * This source file is part of the FoundationDB open source project
 *
 * Copyright 2013-2018 Apple Inc. and the FoundationDB project authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FDBSERVER_LOGSYSTEMCONFIG_H
#define FDBSERVER_LOGSYSTEMCONFIG_H
#pragma once

#include "TLogInterface.h"
#include "fdbrpc/ReplicationPolicy.h"
#include "DatabaseConfiguration.h"

template <class Interface>
struct OptionalInterface {
	// Represents an interface with a known id() and possibly known actual endpoints.
	// For example, an OptionalInterface<TLogInterface> represents a particular tlog by id, which you might or might not presently know how to communicate with

	UID id() const { return ident; }
	bool present() const { return iface.present(); }
	Interface const& interf() const { return iface.get(); }

	explicit OptionalInterface( UID id ) : ident(id) {}
	explicit OptionalInterface( Interface const& i ) : ident(i.id()), iface(i) {}
	OptionalInterface() {}

	std::string toString() const { return ident.toString(); }

	bool operator==(UID const& r) const { return ident == r; }

	template <class Ar>
	void serialize( Ar& ar ) {
		ar & iface;
		if( !iface.present() ) ar & ident;
		else ident = iface.get().id();
	}

protected:
	UID ident;
	Optional<Interface> iface;
};

enum { HasBestPolicyNone = 0, HasBestPolicyId = 1 };

struct TLogSet {
	std::vector<OptionalInterface<TLogInterface>> tLogs;
	std::vector<OptionalInterface<TLogInterface>> logRouters;
	int32_t tLogWriteAntiQuorum, tLogReplicationFactor;
	std::vector< LocalityData > tLogLocalities; // Stores the localities of the log servers
	IRepPolicyRef tLogPolicy;
	int8_t locality;
	bool isLocal;
	int32_t hasBestPolicy;

	TLogSet() : tLogWriteAntiQuorum(0), tLogReplicationFactor(0), isLocal(true), hasBestPolicy(HasBestPolicyId), locality(-99) {}

	std::string toString() const {
		return format("anti: %d replication: %d local: %d best: %d routers: %d tLogs: %s locality: %d", tLogWriteAntiQuorum, tLogReplicationFactor, isLocal, hasBestPolicy, logRouters.size(), describe(tLogs).c_str(), locality);
	}

	bool operator == ( const TLogSet& rhs ) const {
		if (tLogWriteAntiQuorum != rhs.tLogWriteAntiQuorum || tLogReplicationFactor != rhs.tLogReplicationFactor || isLocal != rhs.isLocal || hasBestPolicy != rhs.hasBestPolicy || tLogs.size() != rhs.tLogs.size() || locality != rhs.locality) {
			return false;
		}
		if ((tLogPolicy && !rhs.tLogPolicy) || (!tLogPolicy && rhs.tLogPolicy) || (tLogPolicy && (tLogPolicy->info() != rhs.tLogPolicy->info()))) {
			return false;
		}
		for(int j = 0; j < tLogs.size(); j++ ) {
			if (tLogs[j].id() != rhs.tLogs[j].id() || tLogs[j].present() != rhs.tLogs[j].present() || ( tLogs[j].present() && tLogs[j].interf().commit.getEndpoint().token != rhs.tLogs[j].interf().commit.getEndpoint().token ) ) {
				return false;
			}
		}
		return true;
	}

	bool isEqualIds(TLogSet const& r) const {
		if (tLogWriteAntiQuorum != r.tLogWriteAntiQuorum || tLogReplicationFactor != r.tLogReplicationFactor || isLocal != r.isLocal || hasBestPolicy != r.hasBestPolicy || tLogs.size() != r.tLogs.size() || locality != r.locality) {
			return false;
		}
		if ((tLogPolicy && !r.tLogPolicy) || (!tLogPolicy && r.tLogPolicy) || (tLogPolicy && (tLogPolicy->info() != r.tLogPolicy->info()))) {
			return false;
		}
		for(int i = 0; i < tLogs.size(); i++) {
			if( tLogs[i].id() != r.tLogs[i].id() ) {
				return false;
			}
		}
		return true;
	}

	template <class Ar>
	void serialize( Ar& ar ) {
		ar & tLogs & logRouters & tLogWriteAntiQuorum & tLogReplicationFactor & tLogPolicy & tLogLocalities & isLocal & hasBestPolicy & locality;
	}
};

struct OldTLogConf {
	std::vector<TLogSet> tLogs;
	Version epochEnd;

	OldTLogConf() : epochEnd(0) {}

	std::string toString() const {
		return format("end: %d %s", epochEnd, describe(tLogs).c_str()); 
	}

	bool operator == ( const OldTLogConf& rhs ) const {
		return tLogs == rhs.tLogs && epochEnd == rhs.epochEnd;
	}

	bool isEqualIds(OldTLogConf const& r) const {
		if(tLogs.size() != r.tLogs.size()) {
			return false;
		}
		for(int i = 0; i < tLogs.size(); i++) {
			if(!tLogs[i].isEqualIds(r.tLogs[i])) {
				return false;
			}
		}
		return true;
	}

	template <class Ar>
	void serialize( Ar& ar ) {
		ar & tLogs & epochEnd;
	}
};

struct LogSystemConfig {
	int logSystemType;
	std::vector<TLogSet> tLogs;
	std::vector<OldTLogConf> oldTLogs;
	int expectedLogSets;
	int minRouters;

	LogSystemConfig() : logSystemType(0), minRouters(0), expectedLogSets(0) {}

	std::string toString() const { 
		return format("type: %d oldGenerations: %d %s", logSystemType, oldTLogs.size(), describe(tLogs).c_str());
	}

	std::vector<TLogInterface> allPresentLogs() const {
		std::vector<TLogInterface> results;
		for( int i = 0; i < tLogs.size(); i++ ) {
			for( int j = 0; j < tLogs[i].tLogs.size(); j++ ) {
				if( tLogs[i].tLogs[j].present() ) {
					results.push_back(tLogs[i].tLogs[j].interf());
				}
			}
		}
		return results;
	}

	std::vector<std::pair<UID, NetworkAddress>> allSharedLogs() const {
		typedef std::pair<UID, NetworkAddress> IdAddrPair;
		std::vector<IdAddrPair> results;
		for (auto &tLogSet : tLogs) {
			for (auto &tLog : tLogSet.tLogs) {
				if (tLog.present())
					results.push_back(IdAddrPair(tLog.interf().getSharedTLogID(), tLog.interf().address()));
			}
		}

		for (auto &oldLog : oldTLogs) {
			for (auto &tLogSet : oldLog.tLogs) {
				for (auto &tLog : tLogSet.tLogs) {
					if (tLog.present())
						results.push_back(IdAddrPair(tLog.interf().getSharedTLogID(), tLog.interf().address()));
				}
			}
		}
		uniquify(results);
		// This assert depends on the fact that uniquify will sort the elements based on <UID, NetworkAddr> order
		ASSERT_WE_THINK(std::unique(results.begin(), results.end(), [](IdAddrPair &x, IdAddrPair &y) { return x.first == y.first; }) == results.end());
		return results;
	}

	bool operator == ( const LogSystemConfig& rhs ) const { return isEqual(rhs); }

	bool isEqual(LogSystemConfig const& r) const {
		return logSystemType == r.logSystemType && tLogs == r.tLogs && oldTLogs == r.oldTLogs && minRouters == r.minRouters && expectedLogSets == r.expectedLogSets;
	}

	bool isEqualIds(LogSystemConfig const& r) const {
		for( auto& i : r.tLogs ) {
			for( auto& j : tLogs ) {
				if( i.isEqualIds(j) ) {
					return true;
				}
			}
		}
		return false;
	}

	bool isNextGenerationOf(LogSystemConfig const& r) const {
		if( !oldTLogs.size() ) {
			return false;
		}

		for( auto& i : r.tLogs ) {
				for( auto& j : oldTLogs[0].tLogs ) {
				if( i.isEqualIds(j) ) {
					return true;
				}
			}
		}
		return false;
	}

	template <class Ar>
	void serialize( Ar& ar ) {
		ar & logSystemType & tLogs & oldTLogs & minRouters & expectedLogSets;
	}
};

#endif
