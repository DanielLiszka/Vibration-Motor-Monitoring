import argparse
import logging
from pathlib import Path
from typing import Dict, Optional

from flask import Flask, jsonify, redirect, request, send_file

from cloud.api import labeling_api, monitoring_api
from cloud.api.data_api import register_flask_routes as register_data_routes
from cloud.api.deployment_api import register_flask_routes as register_deployment_routes
from cloud.services import (
    DataCollector,
    DeploymentManager,
    LabelingService,
    MQTTDeploymentNotifier,
    RetrainingOrchestrator,
)
from cloud.services.model_factory import create_default_classifier

logger = logging.getLogger(__name__)


def create_app(
    config: Optional[Dict]=None,
    services: Optional[Dict]=None,
    data_dir: Optional[str]=None,
    model_factory=None,
    start_workers: Optional[bool]=None,
) -> Flask:
    app = Flask(__name__)
    app.config.update(
        DATA_DIR=str(Path(__file__).resolve().parents[1] / '.cloud_data'),
        AUTO_START_BACKGROUND_THREADS=True,
        RETRAINING_CHECK_INTERVAL_SECONDS=3600,
        MQTT_NOTIFY_ENABLED=False,
        MQTT_NOTIFY_BROKER='',
        MQTT_NOTIFY_PORT=1883,
        MQTT_NOTIFY_USERNAME='',
        MQTT_NOTIFY_PASSWORD='',
        MQTT_NOTIFY_TOPIC_PREFIX='motor-vibration-monitor',
        PUBLIC_BASE_URL='',
    )
    if data_dir:
        app.config['DATA_DIR'] = data_dir
    if start_workers is not None:
        app.config['AUTO_START_BACKGROUND_THREADS'] = start_workers
    if model_factory:
        app.config['MODEL_FACTORY'] = model_factory
    if config:
        config = dict(config)
        if 'STATE_DIR' in config and 'DATA_DIR' not in config:
            config['DATA_DIR'] = config['STATE_DIR']
        if 'START_BACKGROUND_WORKERS' in config and 'AUTO_START_BACKGROUND_THREADS' not in config:
            config['AUTO_START_BACKGROUND_THREADS'] = config['START_BACKGROUND_WORKERS']
        app.config.update(config)

    services = dict(services or {})
    data_dir = Path(app.config['DATA_DIR']).resolve()
    models_dir = data_dir / 'models'
    training_dir = data_dir / 'training_runs'
    ui_dir = Path(__file__).resolve().parent / 'ui'

    data_dir.mkdir(parents=True, exist_ok=True)
    models_dir.mkdir(parents=True, exist_ok=True)
    training_dir.mkdir(parents=True, exist_ok=True)

    data_collector = services.get('data_collector') or DataCollector(
        database_path=str(data_dir / 'training_data.db')
    )
    deployment_manager = services.get('deployment_manager') or DeploymentManager(
        models_dir=str(models_dir),
        registry_file=str(models_dir / 'registry.json'),
        state_file=str(models_dir / 'deployments.json'),
    )
    labeling_service = services.get('labeling_service') or LabelingService(
        data_collector=data_collector,
        database_path=str(data_dir / 'labeling.db'),
    )
    model_factory = services.get('model_factory') or app.config.get('MODEL_FACTORY') or create_default_classifier
    mqtt_notifier = services.get('mqtt_notifier')
    if not mqtt_notifier and app.config.get('MQTT_NOTIFY_ENABLED') and app.config.get('MQTT_NOTIFY_BROKER'):
        mqtt_notifier = MQTTDeploymentNotifier(
            broker=app.config['MQTT_NOTIFY_BROKER'],
            port=int(app.config.get('MQTT_NOTIFY_PORT', 1883)),
            username=app.config.get('MQTT_NOTIFY_USERNAME') or None,
            password=app.config.get('MQTT_NOTIFY_PASSWORD') or None,
            topic_prefix=app.config.get('MQTT_NOTIFY_TOPIC_PREFIX', 'motor-vibration-monitor'),
            public_base_url=app.config.get('PUBLIC_BASE_URL') or None,
        )
    retraining_orchestrator = services.get('retraining_orchestrator') or RetrainingOrchestrator(
        data_collector=data_collector,
        deployment_manager=deployment_manager,
        model_factory=model_factory,
        models_dir=str(training_dir),
    )

    if services.get('notify_device'):
        deployment_manager.notify_device = services['notify_device']
    elif mqtt_notifier:
        deployment_manager.notify_device = mqtt_notifier.publish_model_update
    else:
        deployment_manager.notify_device = lambda device_id, update_info: logger.info('Notify %s with %s', device_id, update_info)

    app.extensions['embedded_project.services'] = {
        'data_collector': data_collector,
        'deployment_manager': deployment_manager,
        'labeling_service': labeling_service,
        'retraining_orchestrator': retraining_orchestrator,
        'model_factory': model_factory,
        'mqtt_notifier': mqtt_notifier,
    }
    app.extensions['motor_monitor'] = app.extensions['embedded_project.services']
    app.extensions['services'] = app.extensions['embedded_project.services']

    if app.config['AUTO_START_BACKGROUND_THREADS']:
        data_collector.start()
        retraining_orchestrator.start(app.config['RETRAINING_CHECK_INTERVAL_SECONDS'])

    monitoring_api.register_flask_routes(
        app,
        data_collector=data_collector,
        deployment_manager=deployment_manager,
        retraining_orchestrator=retraining_orchestrator,
        labeling_service=labeling_service,
    )
    labeling_api.register_flask_routes(app, labeling_service=labeling_service)
    register_data_routes(app, data_collector=data_collector)
    register_deployment_routes(app, deployment_manager=deployment_manager)

    @app.route('/', methods=['GET'])
    def root():
        return redirect('/ui/labeling')

    @app.route('/labeling', methods=['GET'])
    def labeling_ui():
        return send_file(ui_dir / 'labeling.html')

    @app.route('/ui/labeling', methods=['GET'])
    def labeling_ui_alias():
        return send_file(ui_dir / 'labeling.html')

    @app.route('/api/ingest/samples', methods=['POST'])
    def ingest_samples_alias():
        payload = request.get_json(silent=True) or {}
        if payload.get('samples'):
            stored = data_collector.receive_batch(payload)
            data_collector.flush()
            return jsonify({'status': 'ok', 'accepted': stored})
        if not payload:
            return jsonify({'status': 'error', 'message': 'Missing sample payload'}), 400
        stored = 1 if data_collector.receive_sample(payload) else 0
        if stored:
            data_collector.flush()
        return jsonify({'status': 'ok', 'accepted': stored})

    return app


def main():
    parser = argparse.ArgumentParser(description='Run the motor monitor cloud service')
    parser.add_argument('--host', default='127.0.0.1', help='Bind host')
    parser.add_argument('--port', type=int, default=5000, help='Bind port')
    parser.add_argument('--data-dir', default=str(Path(__file__).resolve().parents[1] / '.cloud_data'), help='Runtime storage directory')
    parser.add_argument('--public-base-url', default='', help='Public base URL for model download links')
    parser.add_argument('--mqtt-notify-broker', default='', help='Optional MQTT broker for rollout notifications')
    parser.add_argument('--mqtt-notify-port', type=int, default=1883, help='MQTT broker port')
    parser.add_argument('--mqtt-notify-username', default='', help='MQTT username')
    parser.add_argument('--mqtt-notify-password', default='', help='MQTT password')
    parser.add_argument('--mqtt-notify-topic-prefix', default='motor-vibration-monitor', help='MQTT topic prefix')
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO)
    app = create_app(config={
        'DATA_DIR': args.data_dir,
        'PUBLIC_BASE_URL': args.public_base_url or f'http://127.0.0.1:{args.port}',
        'MQTT_NOTIFY_ENABLED': bool(args.mqtt_notify_broker),
        'MQTT_NOTIFY_BROKER': args.mqtt_notify_broker,
        'MQTT_NOTIFY_PORT': args.mqtt_notify_port,
        'MQTT_NOTIFY_USERNAME': args.mqtt_notify_username,
        'MQTT_NOTIFY_PASSWORD': args.mqtt_notify_password,
        'MQTT_NOTIFY_TOPIC_PREFIX': args.mqtt_notify_topic_prefix,
    })
    app.run(host=args.host, port=args.port, debug=False, use_reloader=False)


if __name__ == '__main__':
    main()
