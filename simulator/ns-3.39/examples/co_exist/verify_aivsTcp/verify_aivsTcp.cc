// AI (Ring AllReduce over RDMA) and TCP traffic on a leaf-spine fabric.
/*
 * test-mixed.cc
 *
 *  Created on: May 25, 2022
 *      Author: vamsi
 */
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation;
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <iostream>
#include <fstream>
#include <unordered_map>
#include <time.h>
#include "ns3/core-module.h"
#include "ns3/qbb-helper.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/global-route-manager.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/packet.h"
#include "ns3/error-model.h"
#include <ns3/rdma.h>
#include <ns3/rdma-client.h>
#include <ns3/rdma-client-helper.h>
#include <ns3/rdma-driver.h>
#include <ns3/switch-node.h>
#include "ns3/broadcom-egress-queue.h"
#include <ns3/sim-setting.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <ctime>
#include <set>
#include <string>
#include <unordered_map>
#include <stdlib.h>
#include <unistd.h>
#include <vector>

using namespace ns3;
using namespace std;

#define LOSSLESS 0
#define LOSSY 1
#define DUMMY 2

# define DT 101
# define FAB 102
# define CS 103
# define IB 104
# define ABM 110

NS_LOG_COMPONENT_DEFINE("GENERIC_SIMULATION");

extern "C"
{
#include "cdf.h"
}
#define LINK_CAPACITY_BASE    1000000000          // 1Gbps

std::string data_rate, link_delay, topology_file, flow_file;

Ptr<OutputStreamWrapper> fctOutput;
AsciiTraceHelper asciiTraceHelper;
Ptr<OutputStreamWrapper> tcpFctOutput; // TCP flow completions
// pattern 4: start-time offset of mid-run TCP flows, TCP flows left in the phase
double tcp_time_base_ns = 0;
uint32_t strict_pending = 0;
void StrictTcpDone();

Ptr<OutputStreamWrapper> torStats;
AsciiTraceHelper torTraceHelper;

uint32_t cc_mode = 1;
bool enable_qcn = true;
uint32_t packet_payload_size = 1000, l2_chunk_size = 0, l2_ack_interval = 0;
double pause_time = 5, simulator_stop_time = 3.01;

double alpha_resume_interval = 55, rp_timer, ewma_gain = 1 / 16;
double rate_decrease_interval = 4;
uint32_t fast_recovery_times = 5;
std::string rate_ai, rate_hai, min_rate = "100Mb/s";
std::string dctcp_rate_ai = "1000Mb/s";
bool clamp_target_rate = false, l2_back_to_zero = false;
double error_rate_per_link = 0.0;
uint32_t has_win = 1;
uint32_t global_t = 1;
uint32_t mi_thresh = 5;
bool var_win = false, fast_react = true;
bool multi_rate = true;
bool sample_feedback = false;
double pint_log_base = 1.05;
double pint_prob = 1.0;
double u_target = 0.95;
uint32_t int_multi = 1;
bool rate_bound = true;

uint32_t ack_high_prio = 0;

uint32_t buffer_size = 16;

uint32_t qlen_dump_interval = 100000000, qlen_mon_interval = 100;
uint64_t qlen_mon_start = 2000000000, qlen_mon_end = 2100000000;
string qlen_mon_file;

unordered_map<uint64_t, uint32_t> rate2kmax, rate2kmin;
unordered_map<uint64_t, double> rate2pmax;

double alpha_values[8]={1,1,1,1,1,1,1,1};

/************************************************
 * Runtime varibles
 ***********************************************/
std::ifstream topof, flowf, tracef;

NodeContainer n;
NetDeviceContainer switchToSwitchInterfaces;
std::map< uint32_t, std::map< uint32_t, std::vector<Ptr<QbbNetDevice>> > > switchToSwitch;

// vamsi
std::map<uint32_t, uint32_t> switchNumToId;
std::map<uint32_t, uint32_t> switchIdToNum;
std::map<uint32_t, NetDeviceContainer> switchUp;
std::map<uint32_t, NetDeviceContainer> switchDown;
//NetDeviceContainer switchUp[switch_num];
std::map<uint32_t, NetDeviceContainer> sourceNodes;

NodeContainer servers;
NodeContainer tors;

uint64_t nic_rate;

uint64_t maxRtt, maxBdp;

struct Interface {
    uint32_t idx;
    bool up;
    uint64_t delay;
    uint64_t bw;

    Interface() : idx(0), up(false) {}
};
map<Ptr<Node>, map<Ptr<Node>, Interface> > nbr2if;
// Mapping destination to next hop for each node: <node, <dest, <nexthop0, ...> > >
map<Ptr<Node>, map<Ptr<Node>, vector<Ptr<Node> > > > nextHop;
map<Ptr<Node>, map<Ptr<Node>, uint64_t> > pairDelay;
map<Ptr<Node>, map<Ptr<Node>, uint64_t> > pairTxDelay;
map<uint32_t, map<uint32_t, uint64_t> > pairBw;
map<Ptr<Node>, map<Ptr<Node>, uint64_t> > pairBdp;
map<uint32_t, map<uint32_t, uint64_t> > pairRtt;

std::vector<Ipv4Address> serverAddress;

// maintain port number for each host pair
std::unordered_map<uint32_t, unordered_map<uint32_t, uint16_t> > portNumder;
std::unordered_map<uint32_t, unordered_map<uint32_t, uint16_t> > DestportNumder;


// ---- helpers from examples/Reverie/test-mixed.cc ----
Ipv4Address node_id_to_ip(uint32_t id) {
    return Ipv4Address(0x0b000001 + ((id / 256) * 0x00010000) + ((id % 256) * 0x00000100));
}

uint32_t ip_to_node_id(Ipv4Address ip) {
    return (ip.Get() >> 8) & 0xffff;
}


void TraceMsgFinish (Ptr<OutputStreamWrapper> stream, double size, double start, bool incast, uint32_t prior )
{
    double fct, standalone_fct, slowdown;
    start += tcp_time_base_ns; // 0 except for pattern 4
    fct = Simulator::Now().GetNanoSeconds() - start;
    standalone_fct = maxRtt + size * 8.0 / nic_rate;
    slowdown = fct / standalone_fct;

    *stream->GetStream ()
            << Simulator::Now().GetSeconds()
            << " " << size
            << " " << fct
            << " " << standalone_fct
            << " " << slowdown
            << " " << maxRtt / 1e3
            << " " << (start / 1e3 - Seconds(10).GetMicroSeconds())
            << " " << prior
            << " " << incast
            << std::endl;
    if (strict_pending > 0 && --strict_pending == 0) // last TCP flow of a pattern-4 phase finished
        Simulator::ScheduleNow(&StrictTcpDone);
}

void qp_finish(Ptr<OutputStreamWrapper> fout, Ptr<RdmaQueuePair> q) {
    uint32_t sid = ip_to_node_id(q->sip), did = ip_to_node_id(q->dip);
    uint64_t base_rtt = pairRtt[sid][did], b = pairBw[sid][did];
    uint32_t total_bytes = q->m_size + ((q->m_size - 1) / packet_payload_size + 1) * (CustomHeader::GetStaticWholeHeaderSize() - IntHeader::GetStaticSize()); // translate to the minimum bytes required (with header but no INT)
    uint64_t standalone_fct = base_rtt + total_bytes * 8 * 1e9 / b;
    *fout->GetStream () << "FCT " << (Simulator::Now() - q->startTime).GetNanoSeconds() << " size " << q->m_size << " baseFCT " << standalone_fct << std::endl;

    // remove rxQp from the receiver
    Ptr<Node> dstNode = n.Get(did);
    Ptr<RdmaDriver> rdma = dstNode->GetObject<RdmaDriver> ();
    rdma->m_rdma->DeleteRxQp(q->sip.Get(), q->m_pg, q->sport);
}


void get_pfc(FILE* fout, Ptr<QbbNetDevice> dev, uint32_t type) {
    fprintf(fout, "%lu %u %u %u %u\n", Simulator::Now().GetTimeStep(), dev->GetNode()->GetId(), dev->GetNode()->GetNodeType(), dev->GetIfIndex(), type);
}

void CalculateRoute(Ptr<Node> host) {
    // queue for the BFS.
    vector<Ptr<Node> > q;
    // Distance from the host to each node.
    map<Ptr<Node>, int> dis;
    map<Ptr<Node>, uint64_t> delay;
    map<Ptr<Node>, uint64_t> txDelay;
    map<Ptr<Node>, uint64_t> bw;
    // init BFS.
    q.push_back(host);
    dis[host] = 0;
    delay[host] = 0;
    txDelay[host] = 0;
    bw[host] = 0xfffffffffffffffflu;
    // BFS.
    for (int i = 0; i < (int)q.size(); i++) {
        Ptr<Node> now = q[i];
        int d = dis[now];
        for (auto it = nbr2if[now].begin(); it != nbr2if[now].end(); it++) {
            // skip down link
            if (!it->second.up)
                continue;
            Ptr<Node> next = it->first;
            // If 'next' have not been visited.
            if (dis.find(next) == dis.end()) {
                dis[next] = d + 1;
                delay[next] = delay[now] + it->second.delay;
                txDelay[next] = txDelay[now] + packet_payload_size * 1000000000lu * 8 / it->second.bw;
                bw[next] = std::min(bw[now], it->second.bw);
                // we only enqueue switch, because we do not want packets to go through host as middle point
                if (next->GetNodeType() == 1)
                    q.push_back(next);
            }
            // if 'now' is on the shortest path from 'next' to 'host'.
            if (d + 1 == dis[next]) {
                nextHop[next][host].push_back(now);
            }
        }
    }
    for (auto it : delay)
        pairDelay[it.first][host] = it.second;
    for (auto it : txDelay)
        pairTxDelay[it.first][host] = it.second;
    for (auto it : bw)
        pairBw[it.first->GetId()][host->GetId()] = it.second;
}

void CalculateRoutes(NodeContainer &n) {
    for (int i = 0; i < (int)n.GetN(); i++) {
        Ptr<Node> node = n.Get(i);
        if (node->GetNodeType() == 0)
            CalculateRoute(node);
    }
}

void SetRoutingEntries() {
    // For each node.
    for (auto i = nextHop.begin(); i != nextHop.end(); i++) {
        Ptr<Node> node = i->first;
        auto &table = i->second;
        for (auto j = table.begin(); j != table.end(); j++) {
            // The destination node.
            Ptr<Node> dst = j->first;
            // The IP address of the dst.
            Ipv4Address dstAddr = dst->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
            // The next hops towards the dst.
            vector<Ptr<Node> > nexts = j->second;
            for (int k = 0; k < (int)nexts.size(); k++) {
                Ptr<Node> next = nexts[k];
                uint32_t interface = nbr2if[node][next].idx;
                if (node->GetNodeType() == 1)
                    DynamicCast<SwitchNode>(node)->AddTableEntry(dstAddr, interface);
                else {
                    node->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(dstAddr, interface);
                }
            }
        }
    }
}


uint64_t get_nic_rate(NodeContainer &n) {
    for (uint32_t i = 0; i < n.GetN(); i++)
        if (n.Get(i)->GetNodeType() == 0)
            return DynamicCast<QbbNetDevice>(n.Get(i)->GetDevice(1))->GetDataRate().GetBitRate();
}

// ---- random helpers from examples/Reverie/test-mixed.cc ----
double poission_gen_interval(double avg_rate)
{
    if (avg_rate > 0)
        return -logf(1.0 - (double)rand() / RAND_MAX) / avg_rate;
    else
        return 0;
}

template<typename T>
T rand_range (T min, T max)
{
    return min + ((double)max - min) * rand () / RAND_MAX;
}


// ---- TCP traffic generator from examples/ABM/abm-evaluation.cc ----
uint32_t PORT_START[512] = {4444};
#define PORT_END 65530

void install_applications (int txLeaf, NodeContainer* servers, double requestRate, struct cdf_table *cdfTable,
                           long &flowCount, int SERVER_COUNT, int LEAF_COUNT, double START_TIME, double END_TIME, double FLOW_LAUNCH_END_TIME, int numPrior)
{
	uint64_t flowSize;

	uint32_t prior = rand_range(1, numPrior - 1);

	for (int txServer = 0; txServer < SERVER_COUNT; txServer++)
	{
		double startTime = START_TIME + poission_gen_interval (requestRate);
		while (startTime < FLOW_LAUNCH_END_TIME && startTime > START_TIME)
		{
			// Permutation demand matrix
			int rxLeaf = txLeaf + 1;
			if (rxLeaf == LEAF_COUNT) {
				rxLeaf = 0;
			}
			// int rxLeaf=txLeaf;
			// while (txLeaf==rxLeaf){
			//     rxLeaf = get_target_leaf(LEAF_COUNT);
			// }

			uint32_t rxServer = rand_range(0, SERVER_COUNT);

			uint16_t port = PORT_START[rxLeaf * SERVER_COUNT + rxServer]++;
			if (port > PORT_END) {
				port = 4444;
				PORT_START[rxLeaf * SERVER_COUNT + rxServer] = 4444;
			}

			uint64_t flowSize = gen_random_cdf (cdfTable);
			while (flowSize == 0) {
				flowSize = gen_random_cdf (cdfTable);
			}

			Ptr<Node> rxNode = servers[rxLeaf].Get (rxServer);
			Ptr<Ipv4> ipv4 = rxNode->GetObject<Ipv4> ();
			Ipv4InterfaceAddress rxInterface = ipv4->GetAddress (1, 0);
			Ipv4Address rxAddress = rxInterface.GetLocal ();

			InetSocketAddress ad (rxAddress, port);
			Address sinkAddress(ad);
			Ptr<BulkSendApplication> bulksend = CreateObject<BulkSendApplication>();
			bulksend->SetAttribute("Protocol", TypeIdValue(TcpSocketFactory::GetTypeId()));
			bulksend->SetAttribute ("SendSize", UintegerValue (flowSize));
			bulksend->SetAttribute ("MaxBytes", UintegerValue(flowSize));
			bulksend->SetAttribute("FlowId", UintegerValue(flowCount++));
			bulksend->SetAttribute("priorityCustom", UintegerValue(prior));
			bulksend->SetAttribute("Remote", AddressValue(sinkAddress));
			bulksend->SetAttribute("InitialCwnd", UintegerValue (55));
			bulksend->SetAttribute("priority", UintegerValue(prior));
			bulksend->SetStartTime (Seconds(startTime));
			bulksend->SetStopTime (Seconds (END_TIME));
			servers[txLeaf].Get (txServer)->AddApplication(bulksend);

			PacketSinkHelper sink ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
			ApplicationContainer sinkApp = sink.Install (servers[rxLeaf].Get(rxServer));
			sinkApp.Get(0)->SetAttribute("TotalQueryBytes", UintegerValue(flowSize));
			sinkApp.Get(0)->SetAttribute("priority", UintegerValue(0)); // ack packets are prioritized
			sinkApp.Get(0)->SetAttribute("priorityCustom", UintegerValue(0)); // ack packets are prioritized
			sinkApp.Get(0)->SetAttribute("flowId", UintegerValue(flowCount));
			sinkApp.Get(0)->SetAttribute("senderPriority", UintegerValue(prior));
			flowCount += 1;
			sinkApp.Start (Seconds(startTime));
			sinkApp.Stop (Seconds (END_TIME));
			sinkApp.Get(0)->TraceConnectWithoutContext("FlowFinish", MakeBoundCallback(&TraceMsgFinish, tcpFctOutput));
			startTime += poission_gen_interval (requestRate);
		}
	}
	// std::cout << "Finished installation of applications from leaf-"<< fromLeafId << std::endl;
}


// ---- Ring AllReduce: 30 steps, each step waits for all 16 flows ----
#define AI_DPORT   9000 // tags ring flows
#define AI_PG      3 // lossless queue 3
#define RING_STEPS 30
uint64_t ring_chunk_bytes = 4000000; // bytes per flow per step
std::vector<uint32_t> ring; // ranks in ring order
uint32_t ring_step = 0, ring_outstanding = 0, ring_iter = 0;
Time ring_iter_start;
Ptr<OutputStreamWrapper> cctOutput;
uint32_t ring_iters = 0;
double ring_first_start = 0, ring_period = 0; // iteration k starts at ring_first_start + k * ring_period
void AiStartIteration();
bool strict = false; // pattern 4
void StrictTcpPhase();

void LaunchStep() {
    if (ring_step == RING_STEPS) { // AllReduce done: log iter, start, end, CCT
        *cctOutput->GetStream() << ring_iter << " " << ring_iter_start.GetSeconds() << " "
                                << Simulator::Now().GetSeconds() << " "
                                << (Simulator::Now() - ring_iter_start).GetSeconds() << std::endl;
        ring_iter++;
        if (strict) { StrictTcpPhase(); return; }
        if (ring_iter < ring_iters) { // next iteration at its scheduled time, or now if late
            double next = ring_first_start + ring_iter * ring_period;
            Simulator::Schedule(Seconds(std::max(0.0, next - Simulator::Now().GetSeconds())), &AiStartIteration);
        }
        return;
    }
    ring_outstanding = ring.size();
    for (uint32_t i = 0; i < ring.size(); i++) {
        uint32_t src = ring[i], dst = ring[(i + 1) % ring.size()];
        uint16_t sport = portNumder[src][dst]++;
        RdmaClientHelper clientHelper(AI_PG, serverAddress[src], serverAddress[dst], sport, AI_DPORT,
                                      ring_chunk_bytes, has_win ? maxBdp : 0, maxRtt, Simulator::GetMaximumSimulationTime());
        ApplicationContainer app = clientHelper.Install(n.Get(src));
        app.Start(Seconds(0));
    }
}

// RDMA flow finished: count down ring flows
void ring_step_finish(Ptr<RdmaQueuePair> q) {
    if (q->dport != AI_DPORT) return;
    if (--ring_outstanding == 0) {
        ring_step++;
        LaunchStep();
    }
}

void AiStartIteration() {
    ring_step = 0;
    ring_iter_start = Simulator::Now();
    LaunchStep();
}

// ---- pattern 4 (strict disjoint): AI iteration, then a TCP phase until all its flows finish ----
struct StrictTcp {
    std::vector<NodeContainer>* servers; double rate; struct cdf_table* cdf; long* flowCount;
    uint32_t serverCount, nPrior, ai; double gap, aiWindow, end;
} st;

void FakeAiDone() { ring_iter++; StrictTcpPhase(); } // TCP-alone run: idle AI slot

void StrictTcpPhase() {
    tcp_time_base_ns = Simulator::Now().GetNanoSeconds();
    long before = *st.flowCount;
    double now = Simulator::Now().GetSeconds();
    for (int fromLeafId = 0; fromLeafId < 2; fromLeafId++)
        install_applications(fromLeafId, st.servers->data(), st.rate, st.cdf, *st.flowCount, st.serverCount, 2,
                             0, st.end - now, st.gap, st.nPrior);
    strict_pending = (*st.flowCount - before) / 2;
    if (strict_pending == 0) Simulator::ScheduleNow(&StrictTcpDone);
}

void StrictTcpDone() {
    if (ring_iter >= ring_iters) {
        std::cout << "STRICT_DONE t=" << Simulator::Now().GetSeconds() << std::endl;
        Simulator::Stop();
        return;
    }
    if (st.ai) AiStartIteration();
    else Simulator::Schedule(Seconds(st.aiWindow), &FakeAiDone);
}

int main(int argc, char *argv[])
{
    clock_t begint, endt;
    begint = clock();
    std::ifstream conf;
    bool powertcp = false;
    bool thetapowertcp = false;


    uint32_t SERVER_COUNT = 32;
    uint32_t LEAF_COUNT = 2; // LEAF and SPINE correspond to a single pod. Leafs are ToR switches and Spine are AGG switches. Count is within a single pod.
    uint32_t SPINE_COUNT = 8;
    uint64_t LEAF_SERVER_CAPACITY = 10;
    uint64_t SPINE_LEAF_CAPACITY = 40;

    double START_TIME = 0.1;
    double END_TIME = 0.010;
    double FLOW_LAUNCH_END_TIME = 5;

    double load = 0.2;

    uint32_t requestSize = 1000000;
    double queryRequestRate = 1;
    uint32_t incast = 5;

    uint32_t algorithm = 1;
    uint32_t windowCheck = 0;

    uint32_t bufferalgIngress = DT;
    uint32_t bufferalgEgress = DT;


    std::string confFile = "/home/vamsi/src/phd/codebase/ns3-datacenter/simulator/ns-3.35/examples/buffer-devel/config-workload.txt";
    std::string cdfFileName = "/home/vamsi/src/phd/codebase/ns3-datacenter/simulator/ns-3.35/examples/buffer-devel/websearch.txt";

    unsigned randomSeed = 7;


    std::string loadProfile = ""; // TCP load per phase, e.g. "0.2,0.6,0.2"
    uint32_t pattern = 2; // 1 disjoint, 2 random, 3 sync, 4 strict disjoint
    uint32_t ai = 1; // 0 = no AI traffic (TCP-alone run)
    uint32_t aiIters = 2;
    double aiStart = 0.005; // s
    double aiWindow = 0.2; // AI slot length (s)
    double aiPeriod = 0.4; // time between iteration starts (s)
    std::string cctFile = "cct.txt", tcpFctFile = "tcp-fct.txt";
    uint32_t aiFirst = 0; // 1 = strict priority for AI
    uint32_t tcpWaitLimit = 0; // waiting limit D (us)
    uint32_t aimdTarget = 0; // AIMD target D (us)
    uint32_t reverieSetup = 0; // 1 = Reverie's buffer setup
    uint64_t bufferBytes = 0; // switch buffer (bytes), 0 = config file
    double gamma = 0.999; // Reverie filter weight
    uint32_t tcpDctcp = 0; // 1 = DCTCP, 0 = CUBIC
    uint32_t tcpWaitCap = 0; // cap w for the waiting limit
    uint32_t aiWeight = 0; // fixed AI weight w

    CommandLine cmd;
    cmd.AddValue("conf", "config file path", confFile);
    cmd.AddValue("powertcp", "enable powertcp", powertcp);
    cmd.AddValue("thetapowertcp", "enable theta-powertcp, delay version", thetapowertcp);
    cmd.AddValue ("randomSeed", "Random seed, 0 for random generated", randomSeed);

    cmd.AddValue ("SERVER_COUNT", "servers per tor. Please specify the correct value according to the topology file information", SERVER_COUNT);
    cmd.AddValue ("LEAF_COUNT", "number of ToRs that receive traffic", LEAF_COUNT);
    cmd.AddValue ("SPINE_COUNT", "number of ToRs that receive traffic", SPINE_COUNT);
    cmd.AddValue ("LEAF_SERVER_CAPACITY", "tor to server capacity", LEAF_SERVER_CAPACITY);
    cmd.AddValue ("SPINE_LEAF_CAPACITY", "tor to aggregation switch capacity", SPINE_LEAF_CAPACITY);

    cmd.AddValue ("START_TIME", "sim start time", START_TIME);
    cmd.AddValue ("END_TIME", "sim end time", END_TIME);
    cmd.AddValue ("FLOW_LAUNCH_END_TIME", "flow launch process end time", FLOW_LAUNCH_END_TIME);
    cmd.AddValue ("cdfFileName", "File name for flow distribution", cdfFileName);
    cmd.AddValue ("load", "Load on the links from ToR to Agg switches (This is the only place with Over-subscription and is the correct place to load, 0.0 - 1.0", load);

    cmd.AddValue ("request", "Query Size in Bytes", requestSize);
    cmd.AddValue("queryRequestRate", "Query request rate (poisson arrivals)", queryRequestRate);

    cmd.AddValue ("algorithm", "specify CC mode. This is added for my convinience since I prefer cmd rather than parsing files.", algorithm);
    cmd.AddValue("windowCheck", "windowCheck", windowCheck);

    cmd.AddValue ("bufferalgIngress", "specify buffer management algorithm to be used at the ingress", bufferalgIngress);
    cmd.AddValue ("bufferalgEgress", "specify buffer management algorithm to be used at the egress", bufferalgEgress);

    cmd.AddValue("incast", "incast", incast);

    std::string fctOutFile = "./fcts.txt";
    cmd.AddValue ("fctOutFile", "File path for FCTs", fctOutFile);

    std::string torOutFile = "examples/buffer-devel/losslesssfirstDT-newbuffer.txt";
    cmd.AddValue ("torOutFile", "File path for ToR statistic", torOutFile);

    std::string pfcOutFile = "./pfc.txt";
    cmd.AddValue ("pfcOutFile", "File path for pfc events", pfcOutFile);

    std::string alphasFile = "/home/vamsi/src/phd/codebase/ns3-datacenter/simulator/ns-3.35/examples/buffer-devel/alphas0.25"; // On lakewood
    cmd.AddValue ("alphasFile", "alpha values file (should be exactly nPrior lines)", alphasFile);

    std::string bufferModel = "sonic";
    cmd.AddValue("bufferModel", "the buffer model to be used in the switch MMU", bufferModel);

    cmd.AddValue("loadProfile", "comma-separated TCP loads, one per equal-length phase of the run (needs --pattern=2 and --load > 0)", loadProfile);
    cmd.AddValue("pattern", "TCP timing: 1 = disjoint, 2 = random, 3 = sync", pattern);
    cmd.AddValue("ai", "1 = run AI ring, 0 = no AI traffic", ai);
    cmd.AddValue("aiIters", "number of AllReduce iterations", aiIters);
    cmd.AddValue("aiStart", "start time of iteration 0 (s)", aiStart);
    cmd.AddValue("aiWindow", "AI slot length (s)", aiWindow);
    cmd.AddValue("aiPeriod", "time between iteration starts (s)", aiPeriod);
    cmd.AddValue("ringChunkBytes", "bytes per ring flow per step", ring_chunk_bytes);
    cmd.AddValue("cctFile", "output: one line per finished AllReduce", cctFile);
    cmd.AddValue("tcpFctFile", "output: one line per finished TCP flow", tcpFctFile);
    cmd.AddValue("aiFirst", "1 = switches always serve the AI queue before TCP", aiFirst);
    cmd.AddValue("tcpWaitLimit", "D (us) > 0 = switches serve AI first, unless the oldest TCP packet has waited longer than D", tcpWaitLimit);
    cmd.AddValue("aimdTarget", "D (us) > 0 = each port adapts AI's weight: +1 while TCP's smoothed wait < D, halved when above", aimdTarget);
    cmd.AddValue("reverieSetup", "1 = PFC headroom only on lossless queues 0 and 3 and a shared pool, as in examples/Reverie/reverie-evaluation-sigcomm2023.cc", reverieSetup);
    cmd.AddValue("bufferBytes", "switch buffer in bytes (0 = BUFFER_SIZE MiB from the config file)", bufferBytes);
    cmd.AddValue("gamma", "Reverie low-pass filter weight", gamma);
    cmd.AddValue("tcpDctcp", "1 = TCP uses DCTCP with ECN instead of CUBIC", tcpDctcp);
    cmd.AddValue("tcpWaitCap", "w > 0 (with tcpWaitLimit) = a late TCP queue gets a turn only after w AI packets in a row", tcpWaitCap);
    cmd.AddValue("aiWeight", "w > 0 = switches serve up to w AI packets per TCP packet", aiWeight);
    cmd.Parse (argc, argv);
    if (aiFirst) BEgressQueue::prio_queue = AI_PG;
    if (aiWeight) { BEgressQueue::prio_queue = AI_PG; BEgressQueue::prio_weight = aiWeight; }
    if (aimdTarget) { BEgressQueue::prio_queue = AI_PG; BEgressQueue::aimd_target_ns = aimdTarget * 1000ull; }
    if (tcpWaitLimit) { BEgressQueue::prio_queue = AI_PG; BEgressQueue::wait_limit_ns = tcpWaitLimit * 1000ull; BEgressQueue::wait_cap = tcpWaitCap; }

    fctOutput = asciiTraceHelper.CreateFileStream (fctOutFile);
    tcpFctOutput = asciiTraceHelper.CreateFileStream (tcpFctFile);
    cctOutput = asciiTraceHelper.CreateFileStream (cctFile);
    torStats = torTraceHelper.CreateFileStream (torOutFile);

    std::string line;
    std::fstream aFile;
    aFile.open(alphasFile);
    uint32_t p=0;
    while ( getline( aFile, line ) && p < 8 ) { // hard coded to read only 8 alpha values.
        std::istringstream iss( line );
        double a;
        iss >> a;
        alpha_values[p] = a;
        p++;
    }
    aFile.close();

    SPINE_LEAF_CAPACITY = SPINE_LEAF_CAPACITY * LINK_CAPACITY_BASE;
    LEAF_SERVER_CAPACITY = LEAF_SERVER_CAPACITY * LINK_CAPACITY_BASE;

    conf.open(confFile.c_str());
    while (!conf.eof())
    {
        std::string key;
        conf >> key;

        if (key.compare("ENABLE_QCN") == 0)
        {
            uint32_t v;
            conf >> v;
            enable_qcn = v;
        }
        else if (key.compare("CLAMP_TARGET_RATE") == 0)
        {
            uint32_t v;
            conf >> v;
            clamp_target_rate = v;
        }
        else if (key.compare("PAUSE_TIME") == 0)
        {
            double v;
            conf >> v;
            pause_time = v;
        }
        else if (key.compare("DATA_RATE") == 0)
        {
            std::string v;
            conf >> v;
            data_rate = v;
        }
        else if (key.compare("LINK_DELAY") == 0)
        {
            std::string v;
            conf >> v;
            link_delay = v;
        }
        else if (key.compare("PACKET_PAYLOAD_SIZE") == 0)
        {
            uint32_t v;
            conf >> v;
            packet_payload_size = v;
        }
        else if (key.compare("L2_CHUNK_SIZE") == 0)
        {
            uint32_t v;
            conf >> v;
            l2_chunk_size = v;
        }
        else if (key.compare("L2_ACK_INTERVAL") == 0)
        {
            uint32_t v;
            conf >> v;
            l2_ack_interval = v;
        }
        else if (key.compare("L2_BACK_TO_ZERO") == 0)
        {
            uint32_t v;
            conf >> v;
            l2_back_to_zero = v;
        }
        else if (key.compare("TOPOLOGY_FILE") == 0)
        {
            std::string v;
            conf >> v;
            topology_file = v;
        }
        else if (key.compare("FLOW_FILE") == 0)
        {
            std::string v;
            conf >> v;
            flow_file = v;
        }
        else if (key.compare("SIMULATOR_STOP_TIME") == 0)
        {
            double v;
            conf >> v;
            simulator_stop_time = v;
        }
        else if (key.compare("ALPHA_RESUME_INTERVAL") == 0)
        {
            double v;
            conf >> v;
            alpha_resume_interval = v;
        }
        else if (key.compare("RP_TIMER") == 0)
        {
            double v;
            conf >> v;
            rp_timer = v;
        }
        else if (key.compare("EWMA_GAIN") == 0)
        {
            double v;
            conf >> v;
            ewma_gain = v;
        }
        else if (key.compare("FAST_RECOVERY_TIMES") == 0)
        {
            uint32_t v;
            conf >> v;
            fast_recovery_times = v;
        }
        else if (key.compare("RATE_AI") == 0)
        {
            std::string v;
            conf >> v;
            rate_ai = v;
        }
        else if (key.compare("RATE_HAI") == 0)
        {
            std::string v;
            conf >> v;
            rate_hai = v;
        }
        else if (key.compare("ERROR_RATE_PER_LINK") == 0)
        {
            double v;
            conf >> v;
            error_rate_per_link = v;
        }
        else if (key.compare("CC_MODE") == 0) {
            conf >> cc_mode;
        } else if (key.compare("RATE_DECREASE_INTERVAL") == 0) {
            double v;
            conf >> v;
            rate_decrease_interval = v;
        } else if (key.compare("MIN_RATE") == 0) {
            conf >> min_rate;
        } else if (key.compare("HAS_WIN") == 0) {
            conf >> has_win;
        } else if (key.compare("GLOBAL_T") == 0) {
            conf >> global_t;
        } else if (key.compare("MI_THRESH") == 0) {
            conf >> mi_thresh;
        } else if (key.compare("VAR_WIN") == 0) {
            uint32_t v;
            conf >> v;
            var_win = v;
        } else if (key.compare("FAST_REACT") == 0) {
            uint32_t v;
            conf >> v;
            fast_react = v;
        } else if (key.compare("U_TARGET") == 0) {
            conf >> u_target;
        } else if (key.compare("INT_MULTI") == 0) {
            conf >> int_multi;
        } else if (key.compare("RATE_BOUND") == 0) {
            uint32_t v;
            conf >> v;
            rate_bound = v;
        } else if (key.compare("ACK_HIGH_PRIO") == 0) {
            conf >> ack_high_prio;
        } else if (key.compare("DCTCP_RATE_AI") == 0) {
            conf >> dctcp_rate_ai;
        } else if (key.compare("KMAX_MAP") == 0) {
            int n_k ;
            conf >> n_k;
            for (int i = 0; i < n_k; i++) {
                uint64_t rate;
                uint32_t k;
                conf >> rate >> k;
                rate2kmax[rate] = k;
            }
        } else if (key.compare("KMIN_MAP") == 0) {
            int n_k ;
            conf >> n_k;
            for (int i = 0; i < n_k; i++) {
                uint64_t rate;
                uint32_t k;
                conf >> rate >> k;
                rate2kmin[rate] = k;
            }
        } else if (key.compare("PMAX_MAP") == 0) {
            int n_k ;
            conf >> n_k;
            for (int i = 0; i < n_k; i++) {
                uint64_t rate;
                double p;
                conf >> rate >> p;
                rate2pmax[rate] = p;
            }
        } else if (key.compare("BUFFER_SIZE") == 0) {
            conf >> buffer_size;
        } else if (key.compare("QLEN_MON_FILE") == 0) {
            conf >> qlen_mon_file;
        } else if (key.compare("QLEN_MON_START") == 0) {
            conf >> qlen_mon_start;
        } else if (key.compare("QLEN_MON_END") == 0) {
            conf >> qlen_mon_end;
        } else if (key.compare("MULTI_RATE") == 0) {
            int v;
            conf >> v;
            multi_rate = v;
        } else if (key.compare("SAMPLE_FEEDBACK") == 0) {
            int v;
            conf >> v;
            sample_feedback = v;
        } else if (key.compare("PINT_LOG_BASE") == 0) {
            conf >> pint_log_base;
        } else if (key.compare("PINT_PROB") == 0) {
            conf >> pint_prob;
        }
        fflush(stdout);
    }
    conf.close();

    // overriding config file. I prefer to use cmd arguments
    cc_mode = algorithm; // overrides configuration file
    has_win = windowCheck; // overrides configuration file

    Config::SetDefault("ns3::QbbNetDevice::PauseTime", UintegerValue(pause_time));
    Config::SetDefault("ns3::QbbNetDevice::QcnEnabled", BooleanValue(enable_qcn));

    // set int_multi
    IntHop::multi = int_multi;
    // IntHeader::mode
    if (cc_mode == 7) // timely, use ts
        IntHeader::mode = IntHeader::TS;
    else if (cc_mode == 3) // hpcc, powertcp, use int
        IntHeader::mode = IntHeader::NORMAL;
    else if (cc_mode == 10) // hpcc-pint
        IntHeader::mode = IntHeader::PINT;
    else // others, no extra header
        IntHeader::mode = IntHeader::NONE;

    // Set Pint
    if (cc_mode == 10) {
        Pint::set_log_base(pint_log_base);
        IntHeader::pint_bytes = Pint::get_n_bytes();
        printf("PINT bits: %d bytes: %d\n", Pint::get_n_bits(), Pint::get_n_bytes());
    }


// ---- topology ----
    std::ifstream topof(topology_file.c_str());
    if (!topof.is_open()) {
        std::cerr << "ERROR: could not open topology file " << topology_file << std::endl;
        return 1;
    }

    // header: 6 fields
    uint32_t node_num, switch_num, tors, link_num;
    uint64_t leaf_server_bw, spine_leaf_bw;
    topof >> node_num >> switch_num >> tors >> link_num >> leaf_server_bw >> spine_leaf_bw;

    NodeContainer serverNodes, torNodes, spineNodes, switchNodes;
    std::vector<uint32_t> node_type(node_num, 0); // 0 server, 1 leaf, 2 spine

    // switch ids: the first LEAF_COUNT are leaves
    for (uint32_t i = 0; i < switch_num; i++) {
        uint32_t sid;
        topof >> sid;
        switchNumToId[i] = sid;
        switchIdToNum[sid] = i;
        node_type[sid] = (i < LEAF_COUNT) ? 1 : 2;
    }

    // nodes
    for (uint32_t i = 0; i < node_num; i++) {
        if (node_type[i] == 0) {
            Ptr<Node> node = CreateObject<Node>();
            n.Add(node);
            serverNodes.Add(node);
        } else {
            Ptr<SwitchNode> sw = CreateObject<SwitchNode>();
            n.Add(sw);
            switchNodes.Add(sw);
            sw->SetAttribute("EcnEnabled", BooleanValue(true));
            sw->SetNodeType(1);
            if (node_type[i] == 1) torNodes.Add(sw);
            else spineNodes.Add(sw);
        }
    }

    InternetStackHelper internet;
    internet.Install(n);

    // server IPs
    serverAddress.resize(node_num);
    for (uint32_t i = 0; i < node_num; i++) {
        if (n.Get(i)->GetNodeType() == 0) {
            serverAddress[i] = node_id_to_ip(i);
        }
    }

    // links: src dst rate delay error_rate
    FILE *pfc_file = fopen(pfcOutFile.c_str(), "w");
    QbbHelper qbb;
    Ipv4AddressHelper ipv4;
    for (uint32_t i = 0; i < link_num; i++) {
        uint32_t src, dst;
        std::string data_rate, link_delay;
        double error_rate;
        topof >> src >> dst >> data_rate >> link_delay >> error_rate;

        Ptr<Node> snode = n.Get(src), dnode = n.Get(dst);
        qbb.SetDeviceAttribute("DataRate", StringValue(data_rate));
        qbb.SetChannelAttribute("Delay", StringValue(link_delay));

        NetDeviceContainer d = qbb.Install(snode, dnode);

        if (snode->GetNodeType() == 0) {
            Ptr<Ipv4> ipv4s = snode->GetObject<Ipv4>();
            ipv4s->AddInterface(d.Get(0));
            ipv4s->AddAddress(1, Ipv4InterfaceAddress(serverAddress[src], Ipv4Mask(0xff000000)));
        }
        if (dnode->GetNodeType() == 0) {
            Ptr<Ipv4> ipv4d = dnode->GetObject<Ipv4>();
            ipv4d->AddInterface(d.Get(1));
            ipv4d->AddAddress(1, Ipv4InterfaceAddress(serverAddress[dst], Ipv4Mask(0xff000000)));
        }

    // per-link subnet
        std::stringstream ipstring;
        ipstring << "10." << i / 254 + 1 << "." << i % 254 + 1 << ".0";
        ipv4.SetBase(ipstring.str().c_str(), "255.255.255.0");
        ipv4.Assign(d);

    // link-state graph for BFS routing
        nbr2if[snode][dnode].idx = DynamicCast<QbbNetDevice>(d.Get(0))->GetIfIndex();
        nbr2if[snode][dnode].up = true;
        nbr2if[snode][dnode].delay = DynamicCast<QbbChannel>(DynamicCast<QbbNetDevice>(d.Get(0))->GetChannel())->GetDelay().GetTimeStep();
        nbr2if[snode][dnode].bw = DynamicCast<QbbNetDevice>(d.Get(0))->GetDataRate().GetBitRate();
        nbr2if[dnode][snode].idx = DynamicCast<QbbNetDevice>(d.Get(1))->GetIfIndex();
        nbr2if[dnode][snode].up = true;
        nbr2if[dnode][snode].delay = DynamicCast<QbbChannel>(DynamicCast<QbbNetDevice>(d.Get(1))->GetChannel())->GetDelay().GetTimeStep();
        nbr2if[dnode][snode].bw = DynamicCast<QbbNetDevice>(d.Get(1))->GetDataRate().GetBitRate();

    // PFC trace
        DynamicCast<QbbNetDevice>(d.Get(0))->TraceConnectWithoutContext("QbbPfc", MakeBoundCallback(&get_pfc, pfc_file, DynamicCast<QbbNetDevice>(d.Get(0))));
        DynamicCast<QbbNetDevice>(d.Get(1))->TraceConnectWithoutContext("QbbPfc", MakeBoundCallback(&get_pfc, pfc_file, DynamicCast<QbbNetDevice>(d.Get(1))));
    }
    topof.close();

    nic_rate = get_nic_rate(n);
    SERVER_COUNT = serverNodes.GetN() / LEAF_COUNT; // servers per leaf


// ---- RDMA hosts, routing, switch MMU/ECN/PFC (from examples/Reverie/test-mixed.cc) ----
#if ENABLE_QP
    //
    // install RDMA driver
    //
    for (uint32_t i = 0; i < node_num; i++) {
        if (n.Get(i)->GetNodeType() == 0) { // is server
            // create RdmaHw
            Ptr<RdmaHw> rdmaHw = CreateObject<RdmaHw>();
            rdmaHw->SetAttribute("ClampTargetRate", BooleanValue(clamp_target_rate));
            rdmaHw->SetAttribute("AlphaResumInterval", DoubleValue(alpha_resume_interval));
            rdmaHw->SetAttribute("RPTimer", DoubleValue(rp_timer));
            rdmaHw->SetAttribute("FastRecoveryTimes", UintegerValue(fast_recovery_times));
            rdmaHw->SetAttribute("EwmaGain", DoubleValue(ewma_gain));
            rdmaHw->SetAttribute("RateAI", DataRateValue(DataRate(rate_ai)));
            rdmaHw->SetAttribute("RateHAI", DataRateValue(DataRate(rate_hai)));
            rdmaHw->SetAttribute("L2BackToZero", BooleanValue(l2_back_to_zero));
            rdmaHw->SetAttribute("L2ChunkSize", UintegerValue(l2_chunk_size));
            rdmaHw->SetAttribute("L2AckInterval", UintegerValue(l2_ack_interval));
            rdmaHw->SetAttribute("CcMode", UintegerValue(cc_mode));
            rdmaHw->SetAttribute("RateDecreaseInterval", DoubleValue(rate_decrease_interval));
            rdmaHw->SetAttribute("MinRate", DataRateValue(DataRate(min_rate)));
            rdmaHw->SetAttribute("Mtu", UintegerValue(packet_payload_size));
            rdmaHw->SetAttribute("MiThresh", UintegerValue(mi_thresh));
            rdmaHw->SetAttribute("VarWin", BooleanValue(var_win));
            rdmaHw->SetAttribute("FastReact", BooleanValue(fast_react));
            rdmaHw->SetAttribute("MultiRate", BooleanValue(multi_rate));
            rdmaHw->SetAttribute("SampleFeedback", BooleanValue(sample_feedback));
            rdmaHw->SetAttribute("TargetUtil", DoubleValue(u_target));
            rdmaHw->SetAttribute("RateBound", BooleanValue(rate_bound));
            rdmaHw->SetAttribute("DctcpRateAI", DataRateValue(DataRate(dctcp_rate_ai)));
            rdmaHw->SetAttribute("PowerTCPEnabled", BooleanValue(powertcp));
            rdmaHw->SetAttribute("PowerTCPdelay", BooleanValue(thetapowertcp));
            rdmaHw->SetPintSmplThresh(pint_prob);
            // create and install RdmaDriver
            Ptr<RdmaDriver> rdma = CreateObject<RdmaDriver>();
            Ptr<Node> node = n.Get(i);
            rdma->SetNode(node);
            rdma->SetRdmaHw(rdmaHw);

            node->AggregateObject (rdma);
            rdma->Init();
            rdma->TraceConnectWithoutContext("QpComplete", MakeBoundCallback (qp_finish, fctOutput));
            rdma->TraceConnectWithoutContext("QpComplete", MakeCallback (&ring_step_finish));
        }
    }

#endif

    // set ACK priority on hosts
    if (ack_high_prio)
        RdmaEgressQueue::ack_q_idx = 0;
    else
        RdmaEgressQueue::ack_q_idx = 3;

    // setup routing
    CalculateRoutes(n);
    SetRoutingEntries();
    //
    // get BDP and delay
    //
    maxRtt = maxBdp = 0;
    uint64_t minRtt = 1e9;
    for (uint32_t i = 0; i < node_num; i++) {
        if (n.Get(i)->GetNodeType() != 0)
            continue;
        for (uint32_t j = 0; j < node_num; j++) {
            if (n.Get(j)->GetNodeType() != 0)
                continue;
            if (i == j)
                continue;
            uint64_t delay = pairDelay[n.Get(i)][n.Get(j)];
            uint64_t txDelay = pairTxDelay[n.Get(i)][n.Get(j)];
            uint64_t rtt = delay * 2 + txDelay;
            uint64_t bw = pairBw[i][j];
            uint64_t bdp = rtt * bw / 1000000000 / 8;
            pairBdp[n.Get(i)][n.Get(j)] = bdp;
            pairRtt[i][j] = rtt;
            if (bdp > maxBdp)
                maxBdp = bdp;
            if (rtt > maxRtt)
                maxRtt = rtt;
            if (rtt < minRtt)
                minRtt = rtt;
        }
    }
    printf("maxRtt=%lu maxBdp=%lu minRtt=%lu\n", maxRtt, maxBdp, minRtt);


        // config switch
    // The switch mmu runs Dynamic Thresholds (DT) by default.
    uint64_t totalHeadroom;
    for (uint32_t i = 0; i < node_num; i++) {
        if (n.Get(i)->GetNodeType()) { // is switch
            Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n.Get(i));
            totalHeadroom = 0;
            sw->m_mmu->SetIngressLossyAlg(bufferalgIngress);
            sw->m_mmu->SetIngressLosslessAlg(bufferalgIngress);
            sw->m_mmu->SetEgressLossyAlg(bufferalgEgress);
            sw->m_mmu->SetEgressLosslessAlg(bufferalgEgress);
            sw->m_mmu->SetABMalphaHigh(1024);
            sw->m_mmu->SetABMdequeueUpdateNS(minRtt);
            sw->m_mmu->SetPortCount(sw->GetNDevices()-1); // set the actual port count here so that we don't always iterate over the default 256 ports.
            sw->m_mmu->SetBufferModel(bufferModel);
            sw->m_mmu->SetGamma(gamma);
            // std::cout << "ports " << sw->GetNDevices() << " node " << i << std::endl;
            for (uint32_t j = 1; j < sw->GetNDevices(); j++) {
                Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(sw->GetDevice(j));
                uint64_t rate = dev->GetDataRate().GetBitRate();
                // set port bandwidth in the mmu, used by ABM.
                sw->m_mmu->bandwidth[j] = rate;
                for (uint32_t qu = 0; qu < 8; qu++) {
                    if (qu != 1) { // lossless
                        sw->m_mmu->SetAlphaIngress(alpha_values[qu], j, qu);
                        sw->m_mmu->SetAlphaEgress(10000, j, qu);
                    }
                    else { // lossy
                        sw->m_mmu->SetAlphaIngress(10000, j, qu);
                        sw->m_mmu->SetAlphaEgress(alpha_values[qu], j, qu);
                    }

                    // set ecn
                    NS_ASSERT_MSG(rate2kmin.find(rate) != rate2kmin.end(), "must set kmin for each link speed");
                    NS_ASSERT_MSG(rate2kmax.find(rate) != rate2kmax.end(), "must set kmax for each link speed");
                    NS_ASSERT_MSG(rate2pmax.find(rate) != rate2pmax.end(), "must set pmax for each link speed");
                    sw->m_mmu->ConfigEcn(j, rate2kmin[rate], rate2kmax[rate], rate2pmax[rate]);
                    // set pfc (reverieSetup: lossless queues only)
                    if (!reverieSetup || qu == 0 || qu == 3) {
                    double delay = DynamicCast<QbbChannel>(dev->GetChannel())->GetDelay().GetSeconds();
                    uint32_t headroom = (packet_payload_size + 48) * 2 + 3860 + (2 * rate * delay / 8);
                    // std::cout << headroom << std::endl;
                    sw->m_mmu->SetHeadroom(headroom, j, qu);
                    totalHeadroom += headroom;
                    }
                }

            }
            uint64_t bufBytes = bufferBytes ? bufferBytes : (uint64_t)buffer_size * 1024 * 1024;
            sw->m_mmu->SetBufferPool(bufBytes);
            sw->m_mmu->SetIngressPool(bufBytes - totalHeadroom);
            if (reverieSetup) sw->m_mmu->SetSharedPool(bufBytes - totalHeadroom);
            sw->m_mmu->SetEgressLosslessPool(bufBytes);
            sw->m_mmu->SetEgressLossyPool((bufBytes - totalHeadroom) * 0.8);
            sw->m_mmu->node_id = sw->GetId();
        }
        if (n.Get(i)->GetNodeType())
            std::cout << "total headroom: " << totalHeadroom << " buffer " << (bufferBytes ? bufferBytes : (uint64_t)buffer_size * 1024 * 1024) << " model " << bufferModel << std::endl;
    }

    //
    // setup switch CC
    //
    for (uint32_t i = 0; i < node_num; i++) {
        if (n.Get(i)->GetNodeType()) { // switch
            Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n.Get(i));
            sw->SetAttribute("CcMode", UintegerValue(cc_mode));
            sw->SetAttribute("MaxRtt", UintegerValue(maxRtt));
            sw->SetAttribute("PowerEnabled", BooleanValue(powertcp));
        }
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

// ---- TCP socket defaults (from examples/Reverie/test-mixed.cc) ----
    /*General TCP Socket settings. Mostly used by various congestion control algorithms in common*/
    Config::SetDefault ("ns3::TcpSocket::ConnTimeout", TimeValue (MilliSeconds (10))); // syn retry interval
    Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MicroSeconds (10000)) );  //(MilliSeconds (5))
    Config::SetDefault ("ns3::TcpSocketBase::RTTBytes", UintegerValue ( maxBdp ));  //(MilliSeconds (5))
    Config::SetDefault ("ns3::TcpSocketBase::ClockGranularity", TimeValue (NanoSeconds (10))); //(MicroSeconds (100))
    Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MicroSeconds (10))); //TimeValue (MicroSeconds (80))
    Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (1073725440)); //1073725440
    Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (1073725440));
    Config::SetDefault ("ns3::TcpSocket::ConnCount", UintegerValue (6));  // Syn retry count
    Config::SetDefault ("ns3::TcpSocketBase::Timestamp", BooleanValue (true));
    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1000));
    Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (0));
    Config::SetDefault ("ns3::TcpSocket::PersistTimeout", TimeValue (Seconds (20)));

    // TCP: DCTCP (from reverie-evaluation-sigcomm2023.cc) or CUBIC
    if (tcpDctcp) {
        Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (ns3::TcpDctcp::GetTypeId()));
        Config::SetDefault ("ns3::TcpSocketBase::UseEcn", StringValue ("On"));
    } else
        Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (ns3::TcpCubic::GetTypeId()));

    // servers grouped per leaf
    std::vector<NodeContainer> servers(LEAF_COUNT);
    for (uint32_t i = 0; i < serverNodes.GetN(); i++)
        servers[i / SERVER_COUNT].Add(serverNodes.Get(i));

    // from examples/ABM/abm-evaluation.cc
    uint32_t LINK_COUNT = 1;
    uint32_t nPrior = 2; // TCP data in queue 1
    double oversubRatio = static_cast<double>(SERVER_COUNT * LEAF_SERVER_CAPACITY) / (SPINE_LEAF_CAPACITY * SPINE_COUNT * LINK_COUNT);
    NS_LOG_INFO ("Over-subscription ratio: " << oversubRatio);
    struct cdf_table* cdfTable = new cdf_table ();
    init_cdf (cdfTable);
    load_cdf (cdfTable, cdfFileName.c_str ());
    double requestRate = load * LEAF_SERVER_CAPACITY * SERVER_COUNT / oversubRatio / (8 * avg_cdf (cdfTable)) / SERVER_COUNT;
    NS_LOG_INFO ("Average request rate: " << requestRate << " per second");

    // TCP only on servers 8-15 of each rack; rate scaled so the rack load is unchanged
    uint32_t TCP_PER_RACK = SERVER_COUNT - 8;
    std::vector<NodeContainer> tcpServers(2);
    for (uint32_t r = 0; r < 2; r++)
        for (uint32_t k = 8; k < SERVER_COUNT; k++)
            tcpServers[r].Add(servers[r].Get(k));
    requestRate = requestRate * SERVER_COUNT / TCP_PER_RACK;

    if (randomSeed == 0)
        srand ((unsigned)time (NULL));
    else
        srand (randomSeed);

    long flowCount = 0;
    for (uint32_t i = 0; i < SERVER_COUNT * LEAF_COUNT; i++)
        PORT_START[i] = 4444;

    // windows in which TCP flows may start
    if (pattern < 1 || pattern > 4) { std::cerr << "--pattern must be 1, 2, 3 or 4" << std::endl; return 1; }
    double tcpStart = aiStart, tcpEnd = aiStart + aiIters * aiPeriod;
    std::vector<std::pair<double, double> > windows;
    if (pattern == 2) { // random
        windows.push_back(std::make_pair(tcpStart, tcpEnd));
    } else {
        for (uint32_t i = 0; i < aiIters; i++) {
            double s = aiStart + i * aiPeriod;
            if (pattern == 3) windows.push_back(std::make_pair(s, s + aiWindow)); // sync
            else              windows.push_back(std::make_pair(s + aiWindow, s + aiPeriod)); // disjoint
        }
    }
    // --loadProfile: equal phases, each with its own TCP load
    std::vector<double> profile;
    for (size_t a = 0; a < loadProfile.size(); ) {
        size_t b = loadProfile.find(',', a);
        if (b == std::string::npos) b = loadProfile.size();
        profile.push_back(std::stod(loadProfile.substr(a, b - a)));
        a = b + 1;
    }
    if (!profile.empty()) {
        if (pattern != 2 || load <= 0) { std::cerr << "--loadProfile needs --pattern=2 and --load > 0" << std::endl; return 1; }
        windows.clear();
        double len = (tcpEnd - tcpStart) / profile.size();
        for (uint32_t i = 0; i < profile.size(); i++) {
            windows.push_back(std::make_pair(tcpStart + i * len, tcpStart + (i + 1) * len));
            std::cout << "TCP phase " << i << " start " << windows.back().first << " end " << windows.back().second << " load " << profile.at(i) << std::endl;
        }
    }

    // TCP between rack 0 and rack 1
    if (load > 0 && pattern != 4) { // pattern 4 installs TCP per phase
        for (uint32_t w = 0; w < windows.size(); w++)
            for (int fromLeafId = 0; fromLeafId < 2; fromLeafId++)
                install_applications(fromLeafId, tcpServers.data(), requestRate * (profile.empty() ? 1.0 : profile.at(w) / load), cdfTable, flowCount, TCP_PER_RACK, 2,
                                     windows[w].first, END_TIME, windows[w].second, nPrior);
    }

    // ring: 8 servers of rack 0 interleaved with 8 of rack 1
    for (uint32_t k = 0; k < 8; k++) { ring.push_back(k); ring.push_back(16 + k); }
    // source-port counter per host pair (copied from test-mixed.cc)
    for (uint32_t i = 0; i < node_num; i++) {
        if (n.Get(i)->GetNodeType() == 0)
            for (uint32_t j = 0; j < node_num; j++) {
                if (n.Get(j)->GetNodeType() == 0)
                    portNumder[i][j] = 10000;
            }
    }
    // schedule iteration 0; each iteration schedules the next
    ring_iters = aiIters; ring_first_start = aiStart; ring_period = aiPeriod;
    if (pattern == 4) {
        strict = true;
        st.servers = &tcpServers; st.rate = (load > 0) ? requestRate : 0; st.cdf = cdfTable; st.flowCount = &flowCount;
        st.serverCount = TCP_PER_RACK; st.nPrior = nPrior; st.ai = ai; st.gap = aiPeriod - aiWindow; st.aiWindow = aiWindow; st.end = END_TIME;
        if (!ai) Simulator::Schedule(Seconds(aiStart + aiWindow), &FakeAiDone);
    }
    if (ai)
        Simulator::Schedule(Seconds(aiStart), &AiStartIteration);

    Simulator::Stop(Seconds(END_TIME));
    Simulator::Run();
    std::cout << "TCP flows scheduled: " << flowCount / 2 << std::endl;
    std::cout << "DROPS tcp=" << SwitchNode::drops_lossy << " lossless=" << SwitchNode::drops_lossless << std::endl;
    if (BEgressQueue::prio_queue)
        std::cout << "WHILE_AI_WAITING ai_pkts=" << BEgressQueue::cnt_ai << " other_pkts=" << BEgressQueue::cnt_other
                  << " other_share=" << (double)BEgressQueue::cnt_other / std::max<uint64_t>(1, BEgressQueue::cnt_ai + BEgressQueue::cnt_other) << std::endl;
    Simulator::Destroy();
    fclose(pfc_file);
    free_cdf (cdfTable);
    return 0;
}

