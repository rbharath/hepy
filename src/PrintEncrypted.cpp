/* A simple program that demonstrates a simple homomorphic program on 4 input values.
 * */

#include <iostream>
#include <NTL/ZZ.h>
#include <NTL/BasicThreadPool.h>
#include "FHE.h"
#include "timing.h"
#include "EncryptedArray.h"
#include <NTL/lzz_pXFactoring.h>

#include <cassert>
#include <cstdio>

namespace std {} using namespace std;

int main(int argc, char *argv[])
{
  ArgMapping amap;

  // Technical parameter. Use if you want to explicitly set generator elements
  // for the plaintext arrays.
  Vec<long> gens;
  amap.arg("gens", gens, "use specified vector of generators", NULL);
  amap.note("e.g., gens='[562 1871 751]'");

  // Technical parameter. The order of each generator specified in ords.
  Vec<long> ords;
  amap.arg("ords", ords, "use specified vector of orders", NULL);
  amap.note("e.g., ords='[4 2 -4]', negative means 'bad'");

  // Random seed used in scheme.
  long seed=0;
  amap.arg("seed", seed, "PRG seed");

  // The number of rounds of encrypted computation. If R > 1, then we need to
  // "bootstrap" between rounds which adds a heavy computational overhead.
  long R=1;
  amap.arg("R", R, "number of rounds");

  // If p=2, then plaintext entries are bits.
  long p=2;
  amap.arg("p", p, "plaintext base");

  // Technical parameter. In case r=1, then plaintext entries are just bits.
  long r=1;
  amap.arg("r", r,  "lifting");

  long d=1;
  amap.arg("d", d, "degree of the field extension");

  // Key-switching is an operation which swaps out the key under which a
  // particular ciphertext is encoded. Key-switching is used in a variety of
  // places in homomorphic encryption, notably during "relinearization" which
  // happens after homomorphic multiplication of ciphertexts. The key-switching
  // matrix is a 2xn matrix, where n is some number < L (see description of L
  // below).
  long c=2;
  amap.arg("c", c, "number of columns in the key-switching matrices");

  // The number of "bits" of security the scheme provides (see
  // https://en.wikipedia.org/wiki/Security_level). Basic idea is that for a
  // security level of 80, the attacker needs to perform ~2^80 operations to
  // break the scheme.
  long k=80;
  amap.arg("k", k, "security parameter");

  // The number of levels in the modulus chain. See detailed comment below.
  long L=0;
  amap.arg("L", L, "# of levels in the modulus chain",  "heuristic");

  long s=0;
  amap.arg("s", s, "minimum number of slots");

  // See comment about cyclotomic polynomials below. If chosen_m is set, this
  // value is passed to helper findM that checks if it is secure.
  long chosen_m=0;
  amap.arg("m", chosen_m, "use specified value for cyclotomic polynomial.", NULL);

  amap.parse(argc, argv);

  SetSeed(ZZ(seed));

  // L is the number of "levels" to the FHE scheme. The number of levels
  // governs how many compute operations can be performed on encrypted data
  // before the encryption needs to be refreshed (this refreshing process is
  // called "bootstrapping"). 
  // See comment below about the modulus chain. 
  if (L==0) { // determine L based on R,r
    L = 3*R+3;
    if (p>2 || r>1) { // add some more primes for each round
      long addPerRound = 2*ceil(log((double)p)*r*3)/(log(2.0)*FHE_p2Size) +1;
      L += R * addPerRound;
    }
  }

  cout << "*** L: " << L << endl;

  // Hamming weight of secret key
  long w = 64; 

  // The FHE scheme uses a technical parameter called a cyclotomic polynomial.
  // These polynomials are indexed by whole numbers m. The helper findM helps
  // select a value of m that meets our security requirements.
  long m = FindM(k, L, c, p, d, s, chosen_m, false);

  // Converting gens and ords into vector<long> types.
  vector<long> gens1, ords1;
  convert(gens1, gens);
  convert(ords1, ords);

  // FHEcontext is a convenient book-keeping class that stores a variety of
  // parameters tied to the fully homomorphic encryption scheme.
  FHEcontext context(m, p, r, gens1, ords1);
  // FHE schemes use a sequence of parameters called the modulus chain. These
  // "moduli" are ordered in size, q_0 < q_1 < --- < q_L. At the start of
  // encryption, the largest modulus q_L is used. For technical reasons, as
  // encryption proceeds, have to swap down to smaller and smaller moduli. When
  // q_0 is reached, the FHE scheme can no longer compute on the encrypted
  // data. At this point, a "bootstrapping" step is needed (not used in this
  // file) to refresh.
  buildModChain(context, L, c);

  // G is a technical parameter used to define the plaintexts. Formally, an
  // irreducible polynomial.
  ZZX G;
  G = makeIrredPoly(p, d); 

  context.zMStar.printout();
  std::cout << endl;

  // Print some information about the security level of the current scheme.
  std::cout << "security=" << context.securityLevel()<<endl;

  // Stores the secret key. Almost like the FHEPubKey object, 
  FHESecKey secretKey(context);

  // The public key contains the encryption of the constant 0 (that is, Enc(0))
  // along with key-switching matrices and some bookkeeping information.
  const FHEPubKey& publicKey = secretKey;
  // A Hamming-weight-w secret key. The hamming weight is the number of nonzero
  // entries in the secret key.
  secretKey.GenSecKey(w); 
  // compute key-switching matrices that we need
  addSome1DMatrices(secretKey); 

  // A convenience class that allows for operations on an array of plaintexts.
  // The size of this array is set automatically by the choice of parameters
  // listed above.
  EncryptedArray ea(context, G);
  long nslots = ea.size();
  std::cout << "nslot = " << nslots << endl;

  // A PlaintextArray must be paired with an EncryptedArray.
  NewPlaintextArray p0(ea);
  NewPlaintextArray p1(ea);
  NewPlaintextArray p2(ea);
  NewPlaintextArray p3(ea);

  // Populate our plaintext arrays with random values.
  random(ea, p0);
  random(ea, p1);
  random(ea, p2);
  random(ea, p3);

  // Construct our ciphertext objects
  Ctxt c0(publicKey), c1(publicKey), c2(publicKey), c3(publicKey);

  // Encrypt our plaintexts into the ciphertext
  ea.encrypt(c0, publicKey, p0);
  ea.encrypt(c1, publicKey, p1);
  ea.encrypt(c2, publicKey, p2);
  ea.encrypt(c3, publicKey, p3);


  // random number in [-nslots/2..nslots/2]
  long shamt = RandomBnd(2*(nslots/2) + 1) - (nslots/2);
  std::cout << "shamt = " << shamt << endl;
  // random number in [-(nslots-1)..nslots-1]
  long rotamt = RandomBnd(2*nslots - 1) - (nslots - 1);
  std::cout << "rotamt = " << rotamt << endl;

  // two random constants
  NewPlaintextArray const1(ea);
  NewPlaintextArray const2(ea);
  random(ea, const1);
  random(ea, const2);

  ZZX const1_poly, const2_poly;
  ea.encode(const1_poly, const1);
  ea.encode(const2_poly, const2);

  // Perform computation upon encrypted ciphertexts
  c1.multiplyBy(c0);
  c0.addConstant(const1_poly);
  c2.multByConstant(const2_poly);
  Ctxt tmp(c1);
  ea.shift(tmp, shamt);
  c2 += tmp;
  ea.rotate(c2, rotamt);
  c1.negate();
  c3.multiplyBy(c2);
  c0 -= c3;

  // Perform computations upon plaintext data. Will check that decryption of the encrypted data equals the output of the plaintext computation.
  mul(ea, p1, p0);     // c1.multiplyBy(c0)
  add(ea, p0, const1); // c0 += random constant
  mul(ea, p2, const2); // c2 *= random constant
  NewPlaintextArray tmp_p(p1); // tmp = c1
  shift(ea, tmp_p, shamt); // ea.shift(tmp, random amount in [-nSlots/2,nSlots/2])
  add(ea, p2, tmp_p);  // c2 += tmp
  rotate(ea, p2, rotamt); // ea.rotate(c2, random amount in [1-nSlots, nSlots-1])
  ::negate(ea, p1); // c1.negate()
  mul(ea, p3, p2); // c3.multiplyBy(c2) 
  sub(ea, p0, p3); // c0 -= c3

  c0.cleanUp();
  c1.cleanUp();
  c2.cleanUp();
  c3.cleanUp();

  // Create new plaintexts we use to store decryption of homomorphic outputs.
  NewPlaintextArray pp0(ea);
  NewPlaintextArray pp1(ea);
  NewPlaintextArray pp2(ea);
  NewPlaintextArray pp3(ea);
   
  // Decrypt the ciphertexts
  ea.decrypt(c0, secretKey, pp0);
  ea.decrypt(c1, secretKey, pp1);
  ea.decrypt(c2, secretKey, pp2);
  ea.decrypt(c3, secretKey, pp3);
   
  // Check that the decrypted ciphertexts have the right values.
  if (equals(ea, pp0, p0) && equals(ea, pp1, p1)
      && equals(ea, pp2, p2) && equals(ea, pp3, p3))
       std::cout << "Homomorphic Computation performed correctly.\n";
  else std::cout << "ERROR\n";

}
