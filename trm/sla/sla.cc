//
// Python/C interface file for sla-related stuff
//

#include <Python.h>
#include "numpy/arrayobject.h"
#include "slalib.h"
#include "slamac.h"
#include "trm_vec3.h"
#include "trm_constants.h"

#include <iostream>

// Implements slaDtt

static PyObject* 
sla_dtt(PyObject *self, PyObject *args)
{

    double utc;
    if(!PyArg_ParseTuple(args, "d:sla.dtt", &utc))
	return NULL;

    double d = slaDtt(utc);

    return Py_BuildValue("d", d);

};

// Implements slaCldj

static PyObject* 
sla_cldj(PyObject *self, PyObject *args)
{

    int year, month, day;
    if(!PyArg_ParseTuple(args, "iii:sla.cldj", &year, &month, &day))
	return NULL;

    double mjd;
    int status;
    slaCldj(year, month, day, &mjd, &status);
    if(status == 1){
	PyErr_SetString(PyExc_ValueError, ("sla.cldj: bad year = " + Subs::str(year)).c_str());
	return NULL;
    }else if(status == 2){
	PyErr_SetString(PyExc_ValueError, ("sla.cldj: bad month = " + Subs::str(month)).c_str());
	return NULL;
    }else if(status == 3){
	PyErr_SetString(PyExc_ValueError, ("sla.cldj: bad day = " + Subs::str(day)).c_str());
	return NULL;
    }
    return Py_BuildValue("d", mjd);

};

// Computes TDB time corrected for light travel given a UTC (in MJD), 
// a target position (ICRS), and observatory position

static PyObject* 
sla_utc2tdb(PyObject *self, PyObject *args)
{

    double utc, ra, dec, longitude, latitude, height;
    double pmra = 0., pmdec = 0., epoch = 2000., parallax = 0., rv = 0.;
    if(!PyArg_ParseTuple(args, "dddddd|ddddd:sla.utc2tdb", 
			 &utc, &longitude, &latitude, &height, &ra, &dec, 
			 &pmra, &pmdec, &epoch, &parallax, &rv))
	return NULL;

    // Some checks on the inputs
    if(longitude < -360. || longitude > +360.){
	PyErr_SetString(PyExc_ValueError, "sla.utc2tdb: longituge out of range -360 to +360");
	return NULL;
    }

    if(latitude < -90. || latitude > +90.){
	PyErr_SetString(PyExc_ValueError, "sla.utc2tdb: latitude out of range -90 to +90");
	return NULL;
    }

    if(ra < 0. || ra > 24.){
	PyErr_SetString(PyExc_ValueError, "sla.utc2tdb: ra out of range 0 to 24");
	return NULL;
    }

    if(dec < -90. || dec > +90.){
	PyErr_SetString(PyExc_ValueError, "sla.utc2tdb: declination out of range -90 to +90");
	return NULL;
    }

    // convert angles to those expected by sla routines
    const double CFAC = Constants::PI/180.;
    double latr   = CFAC*latitude;
    double longr  = CFAC*longitude;
    double rar    = CFAC*15.*ra;
    double decr   = CFAC*dec;
    double pmrar  = CFAC*pmra/3600.;
    double pmdecr = CFAC*pmdec/3600.;

    double u, v;
    slaGeoc( latr, height, &u, &v);
    u *= Constants::AU/1000.0;
    v *= Constants::AU/1000.0;
    double tt  = utc + slaDtt(utc)/Constants::DAY;
    double tdb = tt  + slaRcc(tt, utc-int(utc), -longr, u, v)/Constants::DAY;

    // Compute position of Earth relative to the centre of the Sun and
    // the barycentre
    double ph[3], pb[3], vh[3], vb[3];
    slaEpv(tdb, ph, vh, pb, vb);

    // Create 3 vectors for simplicity of code
    Subs::Vec3 hpos(ph), bpos(pb), hvel(vh), bvel(vb);

    // Calculate correction from centre of Earth to observatory
    double last = slaGmst(tdb) + longr + slaEqeqx(tdb);
    double pv[6];
    slaPvobs(latr, height, last, pv);
 
    // Correct for precession/nutation
    double rnpb[3][3];
    slaPneqx(tdb, rnpb);
    slaDimxv(rnpb, pv, pv);
    slaDimxv(rnpb, pv+3, pv+3);

    Subs::Vec3 padd(pv), vadd(pv+3);

    // heliocentric
    hpos += padd;
    hvel += vadd;
    hpos *= Constants::AU;
    hvel *= Constants::AU/Constants::DAY;

    // barycentric
    bpos += padd;
    bvel += vadd;
    bpos *= Constants::AU;
    bvel *= Constants::AU/Constants::DAY;

    // At this point 'hpos' and 'bpos' contains the position of the observatory on the 
    // BCRS reference frame in metres relative to the helio- and barycentres. Now update 
    // the target position using space motion data.
    double nepoch = slaEpj(utc);
    slaPm(rar, decr, pmrar, pmdecr, parallax, rv, epoch, nepoch, &rar, &decr);

    // Compute position vector of target
    double tv[3];
    slaDcs2c(rar, decr, tv);
    Subs::Vec3 targ(tv);

    // Finally, the helio- and barycentrically corrected times 
    double hcorr = dot(targ, hpos)/Constants::C/Constants::DAY;
    double bcorr = dot(targ, bpos)/Constants::C/Constants::DAY;

    double btdb  = tdb + bcorr;
    double htdb  = tdb + hcorr;
    double hutc  = utc + hcorr;

    // and the radial velocities
    double vhel = -dot(targ, hvel)/1000.;
    double vbar = -dot(targ, bvel)/1000.;

    return Py_BuildValue("ddddddd", tt, tdb, btdb, hutc, htdb, vhel, vbar);

};

// Computes observational parameters such as airmass, altititude and elevation

static PyObject* 
sla_amass(PyObject *self, PyObject *args)
{

    double utc, ra, dec, longitude, latitude, height;
    double wave=0.55, pmra = 0., pmdec = 0., epoch = 2000., parallax = 0., rv = 0.;
    if(!PyArg_ParseTuple(args, "dddddd|dddddd:sla.amass", 
			 &utc, &longitude, &latitude, &height, &ra, &dec, 
			 &wave, &pmra, &pmdec, &epoch, &parallax, &rv))
	return NULL;

    // Some checks on the inputs
    if(longitude < -360. || longitude > +360.){
	PyErr_SetString(PyExc_ValueError, "sla.amass: longituge out of range -360 to +360");
	return NULL;
    }

    if(latitude < -90. || latitude > +90.){
	PyErr_SetString(PyExc_ValueError, "sla.amass: latitude out of range -90 to +90");
	return NULL;
    }

    if(ra < 0. || ra > 24.){
	PyErr_SetString(PyExc_ValueError, "sla.amass: ra out of range 0 to 24");
	return NULL;
    }

    if(dec < -90. || dec > +90.){
	PyErr_SetString(PyExc_ValueError, "sla.amass: declination out of range -90 to +90");
	return NULL;
    }

    if(wave <= 0. || dec > wave > 1000000.){
	PyErr_SetString(PyExc_ValueError, "sla.amass: declination out of range -90 to +90");
	return NULL;
    }

    // convert angles to those expected by sla routines
    const double CFAC = Constants::PI/180.;
    double latr   = CFAC*latitude;
    double longr  = CFAC*longitude;
    double rar    = CFAC*15.*ra;
    double decr   = CFAC*dec;
    double pmrar  = CFAC*pmra/3600.;
    double pmdecr = CFAC*pmdec/3600.;
    
    // correct for space motion
    double nepoch = slaEpj(utc);
    slaPm(rar, decr, pmrar, pmdecr, parallax, rv, epoch, nepoch, &rar, &decr);

    // first three small corrections factors are assumed zero for ease of use
    const double DUT  = 0.; // UT1-UTC, seconds
    const double XP   = 0.; // polar motion, radians
    const double YP   = 0.; // polar motion, radians
    const double T    = 285.; // ambient temperature K
    const double P    = 1013.25; // ambient pressure, mbar
    const double RH   = 0.2;     // relative humidity (0-1)
    const double TLR  = 0.0065;  // lapse rate, K/metre

    // *observed* azimuth (N->E), zenith distance, hour angle, declination, ra (all radians)
    double azob, zdob, haob, decob, raob;
    slaI2o(rar, decr, utc, DUT, longr, latr, height, XP, YP, T, P, RH, wave, TLR, 
	   &azob, &zdob, &haob, &decob, &raob);

    // compute refraction
    double refa, refb;
    slaRefcoq(T, P, RH, wave, &refa, &refb);
    double tanz = tan(zdob);
    double delz = tanz*(refa + refb*tanz*tanz)/CFAC;

    // convert units
    haob *= 24./Constants::TWOPI;
    double altob   = 90.-zdob/CFAC;
    double airmass = slaAirmas(zdob); 
    azob /= CFAC;

    // Compute pa
    double paob = slaPa(haob,decr,latr)/CFAC;
    paob = paob > 0. ? paob : 360.+paob;

    // return  airmass, altitude (deg), azimuth (deg, N=0, E=90),
    // hour angle, parallactic angle, angle of refraction
    return Py_BuildValue("dddddd", airmass, altob, azob, haob, paob, delz);

};

//----------------------------------------------------------------------------------------
// The methods

static PyMethodDef SlaMethods[] = {

    {"dtt", sla_dtt, METH_VARARGS, 
     "d = dtt(utc) returns TT-UTC in seconds. UTC in MJD = JD-2400000.5."},

    {"cldj", sla_cldj, METH_VARARGS, 
     "mjd = cldj(year, month, day) returns the mjd of the Gregorian calendar date; MJD = JD-2400000.5."},

    {"utc2tdb", sla_utc2tdb, METH_VARARGS, 
     "(tt,tdb,btdb,hutc,htdb,vhel,vbar) =\n"
     "    utc2tdb(utc,longitude,latitude,height,ra,dec,pmra=0,pmdec=0,epoch=2000,parallax=0,rv=0).\n\n"
     "All times are in MJD. Longitude and latitude are in degrees, east positive; ra and dec are in\n"
     " hours and degrees; proper motions are in arcsec/year (not seconds of RA); parallax is in arcsec\n"
     "and the radial velocity is in km/s. tt is terrestrial time (once ephemeris time); tdb is\n"
     "barycentric dynamical time; btdb is the barycentric dynamical time corrected for light travel\n"
     "time, i.e. as observed at the barycentre of the Solar system, hutc is the utc corrected for light\n"
     " travel to the heliocentre (usual form); htdb is the TDB corrected for light travel to the\n"
     "barycentre (unusual). vhel and vbar are the apparent radial velocity of the target in km/s\n"
     " owing to Earth's motion in relative to the helio- and barycentres."},

    {"amass", sla_amass, METH_VARARGS, 
     "(airmass, alt, az, ha, pa, delz) =\n"
     "  amass(utc,longitude,latitude,height,ra,dec,wave=0.55,pmra=0,pmdec=0,epoch=2000,parallax=0,rv=0).\n\n"
     "All times are in MJD. Longitude and latitude are in degrees, east positive; ra and dec are in\n"
     "hours and degrees; the wavelength of observation wave is in microns; proper motions are in\n"
     "arcsec/year (not seconds of RA); parallax is in arcsec and the radialvelocity is in km/s.\n\n"
     "airmass is the airmass; alt and az are the observed altitude and azimuth in degrees with azimuth\n"
     "measured North through East; ha is the observed hour angle in degrees; pa is the position angle\n"
     "of a parallactic slit; delz is the angle of refraction in degrees."},

    {NULL, NULL, 0, NULL} /* Sentinel */
};

PyMODINIT_FUNC
init_sla(void)
{
    (void) Py_InitModule("_sla", SlaMethods);
    import_array();
}
