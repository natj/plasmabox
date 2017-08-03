import numpy as np

def initial(prm):

    #coordinates
    xx = np.arange(-prm.nxHalo, prm.nx+prm.nxHalo) * prm.dx
    vx = np.zeros((prm.nvfull, prm.ns))
    for kk in range(prm.ns):
        vx[:, kk] = np.linspace(prm.vmin[kk], prm.vmax[kk], prm.nvfull)
    #print xx,vx

    print "full      :", xx[prm.xfull]
    print "---"
    print "left  HALO:", xx[prm.xLb]
    print "right HALO:", xx[prm.xRb]
    print "---"
    print "left  EDGE:", xx[prm.xLe]
    print "right EDGE:", xx[prm.xRe]
    print "---"
    print "mid       :", xx[prm.xmid]
    print "---"
    print "len xmid =", len(xx[prm.xmid])
    print "---"
    print "mid+1       :", xx[prm.xmid + 1]


    for kk in range(prm.ns):
        prm.dv[kk] = vx[1, kk] - vx[0, kk]
        prm.qn[kk] = prm.dv[kk]/( prm.qm[kk] * prm.wp[kk]**2 * prm.dx ) 

    kx = np.zeros(prm.nx)
    kv = np.zeros(prm.nvfull)


    nkx = np.int( np.floor(prm.nx*0.5) )
    nkv = np.int( np.floor(prm.nv*0.5) + prm.nvHalo )

    kx[0:nkx] = np.arange(0, nkx)/(prm.nx * prm.dx)*2.0*np.pi
    kv[0:nkv] = np.arange(0, nkv)/(prm.nvfull)*2.0*np.pi
    for ii in range(nkx, prm.nx):
        kx[ii] = -kx[2*nkx + 2 - ii]

    for jj in range(nkv, prm.nvfull):
        kv[jj] = -kv[2*nkv +2 - jj]
    

    #field initialization
    #fex = np.zeros(prm.nxfull) #full integer grid
    ajx = np.zeros(prm.nxfull)
    #ex  = (fex[0:prm.nx+5] + fex[1:prm.nx+6])/2.0 #half integer grid
    ex = np.zeros(prm.nxfull) #half-integer grid (Yee?)
    #for ii in range(0, prm.nx+5):
    #    ex[ii] = (fex[ii] + fex[ii+1])/2.0
    #ex[-1] = ex[-2] #fill array even though len does not match


    #particle initialization
    ff = np.zeros( (prm.nvfull, prm.nxfull, prm.ns) )
    gx = np.zeros( (prm.nvfull, prm.nxfull, prm.ns) )
    gv = np.zeros( (prm.nvfull, prm.nxfull, prm.ns) )

    gam = 3
    wpe = np.sqrt( np.sum( prm.wp**2 * (-prm.qm) ) )

    for kk in range(prm.ns):
        if prm.qm[kk] < 0:
            ww = np.sqrt( wpe**2 + gam*prm.vt[kk]**2 * kx**2 )
        else:
            ww = prm.vd[kk]*kx
            prm.noise = 3

        #noise 
        prm.nmode = nkx
        amp = max( prm.pamp, prm.namp )
        prm.pamp = amp/prm.nx
        prm.namp = amp/prm.nx

        pphs = np.random.rand(prm.nmode)*360.0
        nphs = np.random.rand(prm.nmode)*360.0

        dn_noise = np.ones(prm.nxfull) #full integer grids
        dd_noise = np.zeros(prm.nxfull)
        vd_noise = np.ones(prm.nxfull) * prm.vd[kk]
        vt_noise = np.ones(prm.nxfull) * prm.vt[kk]

        for ll in range(prm.nmode):
            dn_noise = dn_noise - prm.pamp * np.sin(-kx[1 + ll]*xx + pphs[ll]/180*np.pi) * kx[1 + ll] \
                                + prm.namp * np.sin( kx[1 + ll]*xx + nphs[ll]/180*np.pi) * kx[1 + ll]
        
            dd_noise = dd_noise - prm.pamp * np.cos(-kx[1 + ll]*xx + pphs[ll]/180*np.pi) * kx[1 + ll]**2 \
                                + prm.namp * np.cos( kx[1 + ll]*xx + nphs[ll]/180*np.pi) * kx[1 + ll]**2


            vd_noise = vd_noise - prm.pamp * np.sin(-kx[1 + ll]*xx + pphs[ll]/180*np.pi)*(ww[ll] - prm.vd[kk]*kx[1 + ll]) \
                                - prm.namp * np.sin(-kx[1 + ll]*xx + nphs[ll]/180*np.pi)*(ww[ll] + prm.vd[kk]*kx[1 + ll])


        for ii in prm.xfull:
            for jj in range(prm.nvfull):
                ff[jj, ii, kk] = np.exp(-(vx[jj, kk] - vd_noise[ii])**2/(2*vt_noise[ii]**2)) \
                / (np.sqrt(2*np.pi)*vt_noise[ii])*dn_noise[ii]

                gx[jj, ii, kk] = np.exp(-(vx[jj, kk] - vd_noise[ii])**2/(2*vt_noise[ii]**2)) \
                / (np.sqrt(2*np.pi)*vt_noise[ii])*dn_noise[ii] * prm.dx
                
                gv[jj, ii, kk] = -np.exp(-(vx[jj, kk] - vd_noise[ii])**2/(2*vt_noise[ii]**2)) \
                / (np.sqrt(2*np.pi)*vt_noise[ii])*dn_noise[ii] \
                * (vx[jj, kk] - vd_noise[ii])/(vt_noise[ii]**2) * prm.dv[kk]


    return ff, gx, gv, ex, ajx, xx, vx, kx, kv





