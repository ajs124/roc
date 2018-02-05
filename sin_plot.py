#!env python3

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.pyplot import *
from scipy.signal import *
from scipy.fftpack import *
from optparse import OptionParser
import wavio
import scipy.io.wavfile
import scipy
import math
import sys


def Spectrum(s):
    Ftest = scipy.fftpack.fft( s )
    n = round(s.shape[0]/2)
    xf = np.linspace(0.0, Fs/2.0, n)
    return xf, 20*np.log10(np.abs(Ftest[0:n])/n)


if __name__ == "__main__":
    parser = OptionParser()
    parser.add_option( "-f", "--file", action="store", 
                        default="/tmp/sin_out.wav",
                        type="string", dest="src_file",
                        help="Where to store retrieved signal.")
    
    (options, args) = parser.parse_args()

    print("Openning {}".format(options.src_file))
    
    # out_fl = wavio.read( options.src_file )
    # out = np.array(out_fl.data[0:,0]/math.pow(2.0, out_fl.sampwidth*8-1))
    # out = out - np.mean(out)
    # Fs = Fso

    out_fl = scipy.io.wavfile.read( options.src_file )
    Fs = out_fl[0]
    out = np.array([x/math.pow(2.0,31) for x,y in out_fl[1]])
    # out = np.array([x for x,y in out_fl[1]])
    print(out_fl)

    print("Fs: ", Fs)


    plt.figure()
    plt.subplot(211)
    plt.plot(*Spectrum(out))
    plt.grid()
    plt.xlim([0, 24e3])
    plt.ylim([-150, 0])
    plt.subplot(212)
    plt.plot(out)
    plt.show()
