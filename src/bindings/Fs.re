[@bs.module "fs"]
external readFileSync': (~path: string, ~encoding: string) => string =
  "readFileSync";

[@bs.module "fs"]
external readFileSyncAsBuffer: string => Buffer.t = "readFileSync";

[@bs.module "fs"]
external writeFileSync: (~path: string, ~data: string) => unit =
  "writeFileSync";

[@bs.module "fs"] external existsSync: string => bool = "existsSync";

type mkDirOptions = {recursive: bool};

[@bs.module "fs"]
external mkDirSync: (string, mkDirOptions) => unit = "mkdirSync";

type rmSyncOptions = {
  force: bool,
  recursive: bool,
};

[@bs.module "fs"] external rmSync: (string, rmSyncOptions) => unit = "rmSync";

let readFileSyncAsUtf8 = path => readFileSync'(~path, ~encoding="utf8");

module Promises = {
  [@bs.module "node:fs/promises"]
  external readFileAsBuffer: string => Promise.t(Buffer.t) = "readFile";

  [@bs.module "node:fs/promises"]
  external mkDir: (string, mkDirOptions) => Promise.t(unit) = "mkdir";

  [@bs.module "node:fs/promises"]
  external writeFile: (~path: string, ~data: string) => Promise.t(unit) =
    "writeFile";
};
