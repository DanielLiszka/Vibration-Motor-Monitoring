from typing import Dict, Any, List
from datetime import datetime, timedelta
import json

def create_monitoring_routes(data_collector, deployment_manager, retraining_orchestrator, labeling_service):

    def get_system_health() -> Dict:
        health = {'status': 'healthy', 'timestamp': datetime.now().isoformat(), 'components': {}}
        try:
            stats = data_collector.get_stats_summary() if data_collector else {}
            health['components']['data_collector'] = {'status': 'healthy', 'total_samples': stats.get('total_samples', 0), 'recent_activity': True}
        except Exception as e:
            health['components']['data_collector'] = {'status': 'unhealthy', 'error': str(e)}
            health['status'] = 'degraded'
        try:
            if deployment_manager:
                prod_model = deployment_manager.get_production_model()
                health['components']['deployment'] = {'status': 'healthy', 'production_model': prod_model['version'] if prod_model else None}
        except Exception as e:
            health['components']['deployment'] = {'status': 'unhealthy', 'error': str(e)}
            health['status'] = 'degraded'
        try:
            if retraining_orchestrator:
                job_status = retraining_orchestrator.get_job_status()
                health['components']['retraining'] = {'status': 'healthy', 'current_job': job_status.get('status') if job_status else 'idle'}
        except Exception as e:
            health['components']['retraining'] = {'status': 'unhealthy', 'error': str(e)}
            health['status'] = 'degraded'
        return health

    def get_dashboard_summary() -> Dict:
        summary = {'timestamp': datetime.now().isoformat(), 'data': {}, 'models': {}, 'labeling': {}, 'devices': {}}
        if data_collector:
            stats = data_collector.get_stats_summary()
            summary['data'] = {'total_samples': stats.get('total_samples', 0), 'labeled_samples': stats.get('labeled_samples', 0), 'labeling_rate': stats.get('labeling_rate', 0), 'used_for_training': stats.get('used_for_training', 0)}
        if deployment_manager:
            prod = deployment_manager.get_production_model()
            versions = deployment_manager.list_versions()
            summary['models'] = {'production_version': prod['version'] if prod else None, 'production_accuracy': prod['accuracy'] if prod else None, 'total_versions': len(versions), 'versions': [v['version'] for v in versions[:5]]}
        if labeling_service:
            stats = labeling_service.get_stats()
            summary['labeling'] = {'pending_tasks': stats.get('pending', 0), 'completed_today': stats.get('completed', 0), 'agreement_rate': stats.get('agreement_with_model', 0)}
        if data_collector:
            device_stats = data_collector.get_device_stats()
            summary['devices'] = {'total_devices': len(device_stats), 'active_last_hour': sum((1 for d in device_stats if d.last_seen and datetime.now() - d.last_seen < timedelta(hours=1)))}
        return summary

    def get_device_list() -> Dict:
        if not data_collector:
            return {'status': 'ok', 'devices': []}
        device_stats = data_collector.get_device_stats()
        devices = []
        for d in device_stats:
            device_info = {'device_id': d.device_id, 'total_samples': d.total_samples, 'labeled_samples': d.labeled_samples, 'last_seen': d.last_seen.isoformat() if d.last_seen else None, 'model_version': d.model_version, 'status': 'online' if d.last_seen and datetime.now() - d.last_seen < timedelta(minutes=5) else 'offline'}
            if deployment_manager:
                deploy_status = deployment_manager.get_device_deployment_status(d.device_id)
                if deploy_status:
                    device_info['deployment'] = deploy_status
            devices.append(device_info)
        return {'status': 'ok', 'devices': devices}

    def get_device_detail(device_id: str) -> Dict:
        if not data_collector:
            return {'status': 'error', 'message': 'Data collector not available'}
        device_stats = data_collector.get_device_stats(device_id)
        if not device_stats:
            return {'status': 'error', 'message': 'Device not found'}
        d = device_stats[0]
        detail = {'device_id': d.device_id, 'total_samples': d.total_samples, 'labeled_samples': d.labeled_samples, 'last_seen': d.last_seen.isoformat() if d.last_seen else None, 'model_version': d.model_version, 'labeling_rate': d.labeled_samples / d.total_samples if d.total_samples > 0 else 0}
        recent_samples = data_collector.get_unlabeled_samples(limit=10, device_id=device_id)
        detail['recent_samples'] = len(recent_samples)
        return {'status': 'ok', 'device': detail}

    def get_model_list() -> Dict:
        if not deployment_manager:
            return {'status': 'ok', 'models': []}
        versions = deployment_manager.list_versions()
        return {'status': 'ok', 'models': versions}

    def get_model_detail(version: str) -> Dict:
        if not deployment_manager:
            return {'status': 'error', 'message': 'Deployment manager not available'}
        model = deployment_manager.get_model_info(version)
        if not model:
            return {'status': 'error', 'message': 'Model not found'}
        return {'status': 'ok', 'model': model}

    def get_retraining_status() -> Dict:
        if not retraining_orchestrator:
            return {'status': 'ok', 'retraining': None}
        current = retraining_orchestrator.get_job_status()
        history = retraining_orchestrator.get_job_history(5)
        return {'status': 'ok', 'current_job': current, 'recent_jobs': history, 'should_retrain': retraining_orchestrator.should_retrain()}

    def trigger_retraining(data: Dict) -> Dict:
        if not retraining_orchestrator:
            return {'status': 'error', 'message': 'Retraining not available'}
        trigger = data.get('trigger', 'manual')
        job_id = retraining_orchestrator.trigger_retraining(trigger)
        return {'status': 'ok', 'job_id': job_id}

    def get_alerts() -> Dict:
        alerts = []
        if deployment_manager:
            prod = deployment_manager.get_production_model()
            if prod and prod.get('deployed_at'):
                deployed = datetime.fromisoformat(prod['deployed_at'])
                if datetime.now() - deployed > timedelta(days=30):
                    alerts.append({'level': 'warning', 'message': f'Production model is {(datetime.now() - deployed).days} days old', 'category': 'model'})
        if labeling_service:
            stats = labeling_service.get_stats()
            if stats.get('pending', 0) > 500:
                alerts.append({'level': 'warning', 'message': f"{stats['pending']} samples pending labeling", 'category': 'labeling'})
        if data_collector:
            stats = data_collector.get_stats_summary()
            unused = stats['total_samples'] - stats['used_for_training']
            if unused > 1000:
                alerts.append({'level': 'info', 'message': f'{unused} new samples available for training', 'category': 'data'})
        return {'status': 'ok', 'alerts': alerts}

    def get_metrics() -> Dict:
        metrics = []
        if data_collector:
            stats = data_collector.get_stats_summary()
            metrics.append(f"motor_fault_samples_total {stats.get('total_samples', 0)}")
            metrics.append(f"motor_fault_samples_labeled {stats.get('labeled_samples', 0)}")
            metrics.append(f"motor_fault_devices_total {stats.get('total_devices', 0)}")
        if deployment_manager:
            prod = deployment_manager.get_production_model()
            if prod:
                metrics.append(f"motor_fault_model_accuracy {prod.get('accuracy', 0)}")
        if labeling_service:
            stats = labeling_service.get_stats()
            metrics.append(f"motor_fault_labeling_pending {stats.get('pending', 0)}")
            metrics.append(f"motor_fault_labeling_completed {stats.get('completed', 0)}")
        return '\n'.join(metrics)
    return {'get_system_health': get_system_health, 'get_dashboard_summary': get_dashboard_summary, 'get_device_list': get_device_list, 'get_device_detail': get_device_detail, 'get_model_list': get_model_list, 'get_model_detail': get_model_detail, 'get_retraining_status': get_retraining_status, 'trigger_retraining': trigger_retraining, 'get_alerts': get_alerts, 'get_metrics': get_metrics}

def register_flask_routes(app, data_collector, deployment_manager, retraining_orchestrator, labeling_service):
    from flask import request, jsonify, Response
    handlers = create_monitoring_routes(data_collector, deployment_manager, retraining_orchestrator, labeling_service)

    @app.route('/api/monitoring/health', methods=['GET'])
    def health():
        return jsonify(handlers['get_system_health']())

    @app.route('/api/monitoring/dashboard', methods=['GET'])
    def dashboard():
        return jsonify(handlers['get_dashboard_summary']())

    @app.route('/api/monitoring/devices', methods=['GET'])
    def devices():
        return jsonify(handlers['get_device_list']())

    @app.route('/api/monitoring/devices/<device_id>', methods=['GET'])
    def device_detail(device_id):
        return jsonify(handlers['get_device_detail'](device_id))

    @app.route('/api/monitoring/models', methods=['GET'])
    def models():
        return jsonify(handlers['get_model_list']())

    @app.route('/api/monitoring/models/<version>', methods=['GET'])
    def model_detail(version):
        return jsonify(handlers['get_model_detail'](version))

    @app.route('/api/monitoring/retraining', methods=['GET'])
    def retraining():
        return jsonify(handlers['get_retraining_status']())

    @app.route('/api/monitoring/retraining/trigger', methods=['POST'])
    def trigger():
        return jsonify(handlers['trigger_retraining'](request.json or {}))

    @app.route('/api/monitoring/alerts', methods=['GET'])
    def alerts():
        return jsonify(handlers['get_alerts']())

    @app.route('/api/monitoring/metrics', methods=['GET'])
    def metrics():
        return Response(handlers['get_metrics'](), mimetype='text/plain')