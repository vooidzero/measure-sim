#! /usr/bin/python3

import sys
import random
import math
from optparse import OptionParser


class CustomRand:
    def __init__(self):
        pass

    def testCdf(self, cdf):
        if cdf[0][1] != 0:
            return False
        if cdf[-1][1] != 100:
            return False
        for i in range(1, len(cdf)):
            if cdf[i][1] <= cdf[i - 1][1] or cdf[i][0] <= cdf[i - 1][0]:
                return False
        return True

    def setCdf(self, cdf):
        if not self.testCdf(cdf):
            return False
        self.cdf = cdf
        return True

    def getAvg(self):
        s = 0
        last_x, last_y = self.cdf[0]
        for c in self.cdf[1:]:
            x, y = c
            s += (x + last_x) / 2.0 * (y - last_y)
            last_x = x
            last_y = y
        return s / 100

    def rand(self):
        r = random.random() * 100
        for i in range(1, len(self.cdf)):
            if r <= self.cdf[i][1]:
                x0, y0 = self.cdf[i - 1]
                x1, y1 = self.cdf[i]
                return x0 + (x1 - x0) / (y1 - y0) * (r - y0)


class Flow:
    def __init__(self, src, dst, size, t):
        self.src, self.dst, self.size, self.t = src, dst, size, t

    def __str__(self):
        return "%.9f %d %d %d" % (self.t, self.src, self.dst, self.size)


def translate_bandwidth(b: str):
    b = b.replace('bps', '')
    if b[-1] == 'G':
        return float(b[:-1]) * 1e9
    if b[-1] == 'M':
        return float(b[:-1]) * 1e6
    if b[-1] == 'K':
        return float(b[:-1]) * 1e3
    return float(b)


def poisson(lamda):
    return -math.log(1 - random.random()) * lamda


if __name__ == "__main__":
    parser = OptionParser()
    parser.add_option("-l", "--load", dest="load",
                      help="the percentage of the traffic load to the network capacity, by default 0.3", default="0.3")
    parser.add_option("-b", "--bandwidth", dest="bandwidth",
                      help="the bandwidth of host link (G/M/K), by default 10Gbps", default="10Gbps")
    parser.add_option("-t", "--time", dest="time",
                      help="the total run time (s), by default 2", default="2")
    options, args = parser.parse_args()

    base_t = 10**9

    load = float(options.load)
    bandwidth = translate_bandwidth(options.bandwidth)
    time = float(options.time) * 1e9  # translates to ns

    for model in ["AliStorage", "FacebookHadoop", "GoogleRPC", "WebSearch"]:
        fileName = "traffic-cdf/" + model + '.txt'

        # read the cdf, save in cdf as [[x_i, cdf_i] ...]
        cdf = []
        for line in open(fileName, "r").readlines():
            x, y = map(float, line.strip().split(' '))
            cdf.append([x, y])

        # create a custom random generator, which takes a cdf, and generate number according to the cdf
        customRand = CustomRand()
        if not customRand.setCdf(cdf):
            print("Error: Not a valid cdf")
            sys.exit(0)

        # generate flows
        f_list = []
        avgFlowSize = customRand.getAvg() # avg flow size
        avg_inter_arrival = 10**9 * avgFlowSize / (bandwidth * load / 8)

        t = base_t
        while True:
            inter_t = int(poisson(avg_inter_arrival))
            t += inter_t
            if t > base_t + time:
                break
            size = int(customRand.rand())
            if size <= 0:
                size = 1
            f_list.append(Flow(0, 0, size, t * 1e-9))

        with open(f'traff-{model}.txt', 'wt') as output:
            output.write(str(len(f_list)) + '\n')
            for f in f_list:
                output.write(str(f) + '\n')