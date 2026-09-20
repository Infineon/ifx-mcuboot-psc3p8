"""
Copyright 2025 Cypress Semiconductor Corporation (an Infineon company)
or an affiliate of Cypress Semiconductor Corporation. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
"""

import argparse
import json
import sys
import os


def load_json(file_path):
    """
    Loads JSON from file.
    """

    data_json = None

    try:
        with open(file_path, encoding="utf-8") as file:
            data_json = json.load(file)

    except FileNotFoundError:
        print(f'\nERROR: Cannot find {file_path}')
        sys.exit(1)

    return data_json


class FieldsValidator:
    """
        Validation of required fields and their cross-dependencies.
    """

    @staticmethod
    def validate(feature_json, properties_json):
        """
            Check 'target' and properties of a platform.
        """
        p_target = properties_json.get('target')
        if p_target is None:
            raise AttributeError('Field "target" must be present in platform_properties.json')

        f_target = feature_json.get('target')
        if f_target is None:
            raise AttributeError('Field "target" must be present in a feature_config.json')

        if f_target not in p_target:
            raise AttributeError('Target in feature config is not correct.'
                                    ' It must be among the target list of platform_properties.json')

        f_security_setup = feature_json.get('security_setup')
        p_security_setup = properties_json.get('security_setup')

        if f_security_setup:

            if p_security_setup is None:
                print("\nThis platform doesn't have any 'secure_setup' features")
                sys.exit(1)

            if f_security_setup.get('hw_rollback_prot'):
                if p_security_setup.get('hw_rollback_prot') is None:
                    print("\nThis platform doesn't have HW anti roll-back counter")
                    sys.exit(1)

            if f_security_setup.get('hw_crypto_acceleration'):
                if p_security_setup.get('hw_crypto_acceleration') is None:
                    print("\nThe platform doesn't support HW crypto acceleration")
                    sys.exit(1)

            if f_security_setup.get('validate_upgrade').get('value') is False:
                if f_security_setup.get('validate_boot').get('value'):
                    print("\nERROR: Boot slot validation cannot be enabled if upgrade "\
                                "slot validation is disabled")
                    sys.exit(1)


class FeatureProcessor:

    """
        The general handler of all needed fields and filling the new mk-file.
    """

    settings_dict = {
        'validate_boot'             :   'MCUBOOT_SKIP_BOOT_VALIDATION',
        'validate_upgrade'          :   'MCUBOOT_SKIP_UPGRADE_VALIDATION',
        'dependency_check'          :   'MCUBOOT_DEPENDENCY_CHECK',
        'serial_logging'            :   'MCUBOOT_LOG_LEVEL',
        'watch_dog_timer'           :   'USE_WDT',
        'hw_rollback_prot'          :   'USE_HW_ROLLBACK_PROT',
        'hw_crypto_acceleration'    :   "USE_CRYPTO_HW",
        'sw_downgrade_prev'         :   'USE_SW_DOWNGRADE_PREV',
        'ram_app_staging'           :   'USE_STAGE_RAM_APPS',
        'xip'                       :   'USE_XIP',
        'image_encryption'          :   'ENC_IMG',
        'encryption_type'           :   'USE_ENCRYPTION_KDF_CTR',
        'fault_injection_hardening' :   'FIH_PROFILE_LEVEL',
        'combine_hex'               :   'COMBINE_HEX',
        'sflash_keys'               :   'USE_SFLASH_KEYS',
        'built_in_keys'              :  'USE_BUILT_IN_KEYS'
    }

    debug_level_dict = {
        'off'       :   '_OFF',
        'error'     :   '_ERROR',
        'warning'   :   '_WARNING',
        'info'      :   '_INFO',
        'debug'     :   '_DEBUG'
    }

    fih_level_dict = {
        'off'       :   'OFF',
        'low'       :   'LOW',
        'medium'    :   'MEDIUM',
        'high'      :   'HIGH'
    }

    def __init__(self, output_name):
        self.out_f = output_name

    @staticmethod
    def generate_header_guard():
        """
            Print header line at the begining of a mk-file
        """
        guard_lines = ('# AUTO-GENERATED FILE, DO NOT EDIT.'
                        ' ALL CHANGES WILL BE LOST! #\n\n')

        return guard_lines

    @staticmethod
    def insert_res(val_to_check) -> str:
        """
            Simlpe check result and return the string with value.
        """
        return f' := {1 if val_to_check else 0}\n'

    @staticmethod
    def insert_inverted_res(val_to_check) -> str:
        """
            Simlpe check result and return the string with inverted value.
        """
        return f' := {0 if val_to_check else 1}\n'

    def __prnt_dict_primitive_key(self, dict_feature_config, settings_dict_key, f_out):
        """
            Print kyes of 'feature_config' with bool type of 'value'
        """
        val = dict_feature_config.get(settings_dict_key).get('value')

        if isinstance(val, bool):

            # invert because variable use 'skip' command
            need_invertion = set(("validate_boot", "validate_upgrade"))

            f_out.write(self.settings_dict[settings_dict_key])

            if settings_dict_key not in need_invertion:
                f_out.write(FeatureProcessor.insert_res(val))
            else:
                f_out.write(FeatureProcessor.insert_inverted_res(val))


    def __gen_fih_level(self, fih_value):
        """
            Print only FIH_
        """
        res = f"{self.settings_dict['fault_injection_hardening']} ?= "\
              f"{self.fih_level_dict[fih_value]}\n"

        return res

    def __gen_debug_level(self, logging_value):
        """
            Print only MCUBOOT_LOG_LEVEL
        """
        param_txt = self.settings_dict['serial_logging']
        res_str = f"{param_txt} ?= {param_txt}{self.debug_level_dict[logging_value]}\n"

        return res_str


    def __handle_dictionary(self, f_dict, f_out):
        """
            Handle any dictionary of 'feature_config'
        """
        dont_print_list = set(("validation_key", "encryption_key", "encryption_type", "version", "description", "target"))

        # Pre-scan for encryption settings to ensure they're available before processing
        encryption_type = None
        image_encryption_enabled = False

        for k in f_dict:
            if k == 'encryption_type':
                encryption_type = f_dict.get(k).get("value")
            if k == 'image_encryption':
                image_encryption_enabled = f_dict.get(k).get("value")

        for k in f_dict:

            if k not in dont_print_list:
                self.__prnt_dict_primitive_key(f_dict, k, f_out)

            if k == 'fault_injection_hardening':
                f_out.write(self.__gen_fih_level(f_dict.get(k).get("value")))

            if k == 'serial_logging':
                f_out.write(self.__gen_debug_level(f_dict.get(k).get("value")))

            if k == 'validation_key_type':
                validation_key_type = f_dict.get(k).get("value")

            if k == 'validation_key':
                value = f_dict.get(k).get("value")

                if value != '':
                    if validation_key_type == 'ECDSA-256':
                        f_out.write(f'ECDSA_PUBLIC_KEY={value}\n')
                        f_out.write(f'ECDSA_PUBLIC_KEY_SIZE=256\n')
                    elif validation_key_type == 'ECDSA-384':
                        f_out.write(f'ECDSA_PUBLIC_KEY={value}\n')
                        f_out.write(f'ECDSA_PUBLIC_KEY_SIZE=384\n')
                    elif validation_key_type == 'ECDSA-521':
                        f_out.write(f'ECDSA_PUBLIC_KEY={value}\n')
                        f_out.write(f'ECDSA_PUBLIC_KEY_SIZE=521\n')
                    elif validation_key_type == 'XMSS_SHA2_10_256':
                        f_out.write(f'XMSS_PUBLIC_KEY={value}\n')
                    elif validation_key_type == 'LMS_SHA256_M32_H10':
                        f_out.write(f'LMS_PUBLIC_KEY={value}\n')
                    elif validation_key_type == 'ML-DSA-44':
                        f_out.write(f'MLDSA_PUBLIC_KEY={value}\n')
                        f_out.write('MLDSA_PUBLIC_KEY_ALG=44\n')
                    elif validation_key_type == 'ML-DSA-65':
                        f_out.write(f'MLDSA_PUBLIC_KEY={value}\n')
                        f_out.write('MLDSA_PUBLIC_KEY_ALG=65\n')
                    elif validation_key_type == 'ML-DSA-87':
                        f_out.write(f'MLDSA_PUBLIC_KEY={value}\n')
                        f_out.write('MLDSA_PUBLIC_KEY_ALG=87\n')

            if k == 'ml_dsa_sig_hash':
                value = f_dict.get(k).get("value")

                if value != '':
                    if value == 'SHA256':
                        f_out.write('MLDSA_SIG_HASH=256\n')
                    elif value == 'SHA384':
                        f_out.write('MLDSA_SIG_HASH=384\n')
                    elif value == 'SHA512':
                        f_out.write('MLDSA_SIG_HASH=512\n')

            if k == 'encryption_type':
                encryption_type = f_dict.get(k).get("value")

            if k == "encryption_key":
                # Only process encryption settings if image_encryption is enabled
                if not image_encryption_enabled:
                    continue

                # Validate that encryption_type is set when encryption is enabled
                if encryption_type is None or encryption_type == '':
                    print("ERROR: encryption_type must be specified when image_encryption is enabled. Valid values: 'KDF-CMAC', 'EC256'")
                    sys.exit(1)

                value = f_dict.get(k).get("value")

                # Determine encryption method based on encryption_type field
                if encryption_type == "KDF-CMAC":
                    # KDF-CMAC: Hardware-based key derivation (PSC3_P8)
                    f_out.write('USE_ENCRYPTION_KDF_CTR=1\n')
                    if value != '':
                        print("WARNING: encryption_key value is ignored for KDF-CMAC (uses master key from SFLASH)")
                elif encryption_type == "EC256":
                    # EC256: Standard elliptic curve encryption
                    if value == '':
                        print("ERROR: encryption_key must be specified for EC256 encryption")
                        sys.exit(1)
                    f_out.write(f'ENC_PRIVATE_KEY={value}\n')
                else:
                    print(f"ERROR: Unknown encryption_type '{encryption_type}'. Valid values: 'KDF-CMAC', 'EC256'")
                    sys.exit(1)


    def make_file_generate(self, feature_json):
        """
            Processing all keys and creation of a mk-file
        """

        out_dir = os.path.dirname(self.out_f)

        if not os.path.exists(out_dir):
            os.mkdir(out_dir)

        with open(self.out_f, "w", encoding='UTF-8') as f_out:
            f_out.write(FeatureProcessor.generate_header_guard())

            f_security_setup_dict = feature_json.get('security_setup')

            # handling of 'security_setup' section
            if f_security_setup_dict:
                self.__handle_dictionary(f_security_setup_dict, f_out)

            self.__handle_dictionary(feature_json, f_out)


def cli():
    parser = argparse.ArgumentParser(description='Feature config parser to run from CLI')

    parser.add_argument('-f', '--feature_config', required=True,
                        help='Feature configuration file path')
    parser.add_argument('-p', '--platform_properties', required=True,
                        help='Platform properties file path')
    parser.add_argument('-n', '--output_name', required=True,
                        help='The name of the make file that will be generated')
    parser.add_argument('other_args', nargs='?',
                        help='Ignore all other arguments, such as: run')

    args = parser.parse_args()

    run(args.feature_config, args.platform_properties, args.output_name)


def run(feature_config, platform_properties, output_name):
    """
        The main CLI command to run mk-file generation
    """

    feature_config_json = load_json(feature_config)
    platform_properties_json = load_json(platform_properties)

    FieldsValidator.validate(feature_config_json, platform_properties_json)

    fprocessor = FeatureProcessor(output_name)
    fprocessor.make_file_generate(feature_config_json)


if __name__ == '__main__':
    cli()
