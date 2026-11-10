package io.github.bittahbandit1.refare;
public final class AndroidControllerMappingTest {
 public static void main(String[] args){
  double[] state=new double[18];
  ControllerMapping.readHat(state,-1,-1);
  assert state[6]==1&&state[8]==1&&state[7]==0&&state[9]==0:"up-left hat";
  double[] combined=state.clone();ControllerMapping.applyDpadToStick(combined);
  assert combined[14]==-1&&combined[15]==1:"hat to GameCube main stick";
  ControllerMapping.readHat(state,1,1);combined=state.clone();ControllerMapping.applyDpadToStick(combined);
  assert state[7]==1&&state[9]==1&&state[6]==0&&state[8]==0:"direction reversal";
  assert combined[14]==1&&combined[15]==-1:"down-right main stick";
  ControllerMapping.readHat(state,0,0);combined=state.clone();ControllerMapping.applyDpadToStick(combined);
  assert combined[14]==0&&combined[15]==0:"release must clear hat and stick";
  for(int i=6;i<=9;i++)assert state[i]==0:"no stuck D-pad";
  // Key events merge after the hat: a neutral hat must not suppress held keys.
  state[6]=1;combined=state.clone();ControllerMapping.applyDpadToStick(combined);
  assert combined[15]==1:"key-event D-pad remains supported";
  state[14]=.75;state[15]=-.25;ControllerMapping.applyDpadToStick(state);
  assert state[14]==.75&&state[15]==-.25:"preserve active analog stick";
  System.out.println("PASS: hat directions, diagonals, release, key fallback and analog coexistence");
 }
}
